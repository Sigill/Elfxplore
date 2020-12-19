#include "database-utils.hxx"

#include <fstream>
#include <chrono>
#include <filesystem>
#include <omp.h>

#include "ansi.hxx"
#include "Database2.hxx"
#include "logger.hxx"
#include "command-utils.hxx"
#include "utils.hxx"
#include "nm.hxx"
#include "ArtifactSymbols.hxx"

#include <boost/process.hpp>
#include <instrmt/instrmt.hxx>

namespace fs = std::filesystem;
namespace bp = boost::process;
using ansi::style;

namespace {

std::vector<fs::path> load_default_library_directories() {
  LOG_CTX() << style::blue_fg << "Extracting system libraries potential locations" << style::reset;

  bp::ipstream pipe_stream;
  bp::child c("gcc --print-search-dir", bp::std_out > pipe_stream, bp::std_err > bp::null);

  std::vector<fs::path> paths;

  const std::string prefix = "libraries: =";

  std::string line;
  while (pipe_stream && std::getline(pipe_stream, line)) {
    if (!starts_with(line, prefix))
      continue;

    line.erase(0, prefix.size());

    const std::vector<std::string> directories = split(line, ':');
    for(const std::string& dir : directories)
    {
      std::error_code ec;
      fs::path path = fs::canonical(dir, ec);
      if ((bool)ec) {
        LOG(warning) << "Unable to resolve " << dir;
      } else {
        paths.push_back(path);
      }
    }
  }

  c.wait();

  return paths;
}

long long get_or_insert_artifact(Database2& db, const std::string& name, const std::string& type, const long long generating_command_id = -1)
{
  long long artifact_id = db.artifact_id_by_name(name);

  if (artifact_id == -1) {
    db.create_artifact(name, type, generating_command_id);
    artifact_id = db.last_id();
  }

  return artifact_id;
}

class out_pool_scheduler {
private:
  ThreadPool& pool;
public:
  explicit out_pool_scheduler(ThreadPool& pool) : pool(pool) {}
  std::future<void> operator()(std::istream& stream, SymbolReferenceSet& symbols) {
    return pool.enqueue(parse_nm_output, std::ref(stream), std::ref(symbols));
  }
};

class err_pool_scheduler {
private:
  ThreadPool& pool;
public:
  explicit err_pool_scheduler(ThreadPool& pool) : pool(pool) {}
  std::future<std::string> operator()(std::istream& stream) {
    return pool.enqueue(read_stream, std::ref(stream));
  }
};

void extract_symbols_from_file(const Artifact& artifact,
                               SymbolExtractionStatus& status,
                               const std::function<std::future<void>(std::istream& stream, SymbolReferenceSet& symbols)>& out_runner,
                               const std::function<std::future<std::string>(std::istream& stream)>& err_runner) {
  INSTRMT_FUNCTION();

  const std::string& usable_path = artifact.name;
  const bool is_dynamic = artifact.type == "shared";

  std::ifstream file(usable_path, std::ios::in | std::ios::binary);
  char magic[4] = {0, 0, 0, 0};
  file.read(magic, 4);
  file.close();
  status.linker_script = !(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');

  if (!status.linker_script) {
    ArtifactSymbols& symbols = status.symbols;

    status.processes.emplace_back(nm(usable_path, symbols.undefined, nm_options::undefined, out_runner, err_runner));
    if (is_dynamic && symbols.undefined.empty())
      status.processes.emplace_back(nm(usable_path, symbols.undefined, nm_options::undefined_dynamic, out_runner, err_runner));

    status.processes.emplace_back(nm(usable_path, symbols.external, nm_options::defined_extern, out_runner, err_runner));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm(usable_path, symbols.external, nm_options::defined_extern_dynamic, out_runner, err_runner));

    status.processes.emplace_back(nm(usable_path, symbols.external, nm_options::defined, out_runner, err_runner));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm(usable_path, symbols.external, nm_options::defined_dynamic, out_runner, err_runner));

    substract_set(symbols.internal, symbols.external);
  }
}

} // anonymous namespace

void DependenciesExtractor::run(Database2& db)
{
  INSTRMT_REGION("DependenciesExtractor::run");

  const std::vector<fs::path> default_library_directories = load_default_library_directories();

  if (notifyTotalSteps) {
    auto cq = db.statement("select count(*) from commands");
    notifyTotalSteps(db.get_id(cq));
  }

  auto stm = db.statement(R"(
select commands.id, commands.directory, commands.executable, commands.args, artifacts.id, artifacts.name, artifacts.type
from commands
inner join artifacts on artifacts.generating_command_id = commands.id)");

  while (stm.executeStep()) {
    CompilationCommand cmd;
    cmd.id          = stm.getColumn(0).getInt64();
    cmd.directory   = stm.getColumn(1).getString();
    cmd.executable  = stm.getColumn(2).getString();
    cmd.args        = stm.getColumn(3).getString();
    cmd.artifact_id = stm.getColumn(4).getInt64();
    cmd.output      = stm.getColumn(5).getString();
    cmd.output_type = stm.getColumn(6).getString();

    const Dependencies dependencies = parse_dependencies(cmd, default_library_directories);

    std::vector<Artifact> artifacts; artifacts.reserve(dependencies.files.size());

    for (const auto& dependency : dependencies.files) {
      artifacts.emplace_back();
      Artifact& dependency_artifact = artifacts.back();

      dependency_artifact.name = dependency;
      dependency_artifact.type = get_input_type(dependency);
      dependency_artifact.id = get_or_insert_artifact(db, dependency, dependency_artifact.type);

      db.create_dependency(cmd.artifact_id, dependency_artifact.id);
    }

    if (notifyStep)
      notifyStep(cmd, artifacts, dependencies.errors);
  }
}

bool has_failure(const std::vector<ProcessResult>& processes) {
  return std::any_of(processes.begin(), processes.end(), failed);
}

bool has_failure(const SymbolExtractionStatus& status) {
  return status.linker_script || has_failure(status.processes);
}

SymbolExtractor::SymbolExtractor(size_t pool_size)
  : pool_size(pool_size)
  , out_pool(pool_size)
  , err_pool(pool_size)
  , out_runner(out_pool_scheduler(out_pool))
  , err_runner(err_pool_scheduler(err_pool))
{}

void SymbolExtractor::run(Database2& db)
{
  INSTRMT_REGION("SymbolExtractor::run");

  auto q = db.statement("select id, name, type from artifacts where type not in (\"source\", \"static\")");

  auto cq = db.statement("select count(*) from artifacts where type not in (\"source\", \"static\")");
  if (notifyTotalSteps)
    notifyTotalSteps(db.get_id(cq));

#pragma omp parallel num_threads(pool_size)
#pragma omp single
  while (q.executeStep()) {
    Artifact artifact;
    artifact.id   = q.getColumn(0).getInt64();
    artifact.name = q.getColumn(1).getText();
    artifact.type = q.getColumn(2).getText();

#pragma omp task firstprivate(artifact)
    {
      SymbolExtractionStatus status;
      extract_symbols_from_file(artifact, status, out_runner, err_runner);
#pragma omp critical
      {
        db.insert_symbol_references(artifact.id, status.symbols);

        if (notifyStep)
          notifyStep(artifact, status);
      }
    }
  }
}
