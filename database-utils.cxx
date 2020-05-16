#include "database-utils.hxx"

#include <fstream>
#include <chrono>

#include <omp.h>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/process.hpp>

#include "Database2.hxx"
#include "logger.hxx"
#include "command-utils.hxx"
#include "utils.hxx"
#include "nm.hxx"
#include "ArtifactSymbols.hxx"

namespace bfs = boost::filesystem;
namespace bp = boost::process;

namespace {

void clear(CompilationCommand& cmd)
{
  cmd.id = -1;
  cmd.directory.clear();
  cmd.executable.clear();
  cmd.args.clear();
  cmd.output.clear();
  cmd.output_type.clear();
}

void import_command(Database2& db,
                    const std::string& line,
                    const std::function<void (const std::string&, const CompilationCommand&)>& notify,
                    CompilationCommand& cmd)
{
  parse_command(line, cmd);

  notify(line, cmd);

  if (cmd.is_complete()) {
    const long long command_id = db.create_command(cmd.directory, cmd.executable, cmd.args);

    const bfs::path output = expand_path(cmd.output, cmd.directory);

    if (-1 == db.artifact_id_by_name(output.string())) {
      db.create_artifact(output.string(), cmd.output_type, command_id);
    }
  }
}

std::vector<bfs::path> load_default_library_directories() {
  bp::ipstream pipe_stream;
  bp::child c("gcc --print-search-dir", bp::std_out > pipe_stream, bp::std_err > bp::null);

  std::vector<bfs::path> paths;

  const std::string prefix = "libraries: =";

  std::string line;
  while (pipe_stream && std::getline(pipe_stream, line)) {
    if (!starts_with(line, prefix))
      continue;

    line.erase(0, prefix.size());

    const std::vector<std::string> directories = split(line, ':');
    for(const std::string& dir : directories)
    {
      boost::system::error_code ec;
      bfs::path path = bfs::canonical(dir, ec);
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

void extract_symbols_from_file(const Artifact& artifact, SymbolExtractionStatus& status) {
  const std::string& usable_path = artifact.name;
  const bool is_dynamic = artifact.type == "shared";

  std::ifstream file(usable_path, std::ios::in | std::ios::binary);
  char magic[4] = {0, 0, 0, 0};
  file.read(magic, 4);
  file.close();
  status.linker_script = !(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');

  if (!status.linker_script) {
    ArtifactSymbols& symbols = status.symbols;

    status.processes.emplace_back(nm_undefined(usable_path, symbols.undefined, symbol_table::normal));
    if (is_dynamic && symbols.undefined.empty())
      status.processes.emplace_back(nm_undefined(usable_path, symbols.undefined, symbol_table::dynamic));

    status.processes.emplace_back(nm_defined_extern(usable_path, symbols.external, symbol_table::normal));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm_defined_extern(usable_path, symbols.external, symbol_table::dynamic));

    status.processes.emplace_back(nm_defined(usable_path, symbols.external, symbol_table::normal));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm_defined(usable_path, symbols.external, symbol_table::dynamic));

    substract_set(symbols.internal, symbols.external);
  }
}

void clear(SymbolExtractionStatus& status) {
  status.symbols.undefined.clear();
  status.symbols.external.clear();
  status.symbols.internal.clear();
  status.linker_script = false;
  status.processes.clear();
}

class SymbolExtractor {
private:
  Database2& db;
  std::vector<SymbolExtractionStatus> extraction_status_pool;
  std::function<void(const Artifact&, const SymbolExtractionStatus&)> notify;

public:
  explicit SymbolExtractor(Database2& db, std::function<void(const Artifact&, const SymbolExtractionStatus&)> notify)
    : db(db), extraction_status_pool(), notify(notify) {}

  void insert_symbols(const Artifact& artifact,
                      const SymbolExtractionStatus& status) {
    db.insert_symbol_references(artifact.id, status.symbols);

    if (notify)
      notify(artifact, status);
  }

  void operator()(const std::vector<Artifact>& files) {
    extraction_status_pool.resize(files.size());

#pragma omp parallel for
    for(size_t i = 0; i < files.size(); ++i) {
      clear(extraction_status_pool[i]);

      extract_symbols_from_file(files[i], extraction_status_pool[i]);
#pragma omp critical
      insert_symbols(files[i], extraction_status_pool[i]);
    }
  }
};

template<typename T>
class BufferedTasks {
private:
  std::vector<T> buffer;
  size_t capacity;
  std::function<void(const std::vector<T>&)> process;

public:
  BufferedTasks(size_t N, std::function<void(const std::vector<T>&)> processor) : buffer(), capacity(N), process(processor) {
    buffer.reserve(N);
  }

  void processBuffer() {
    process(buffer);
    buffer.clear();
  }

  void push(T t) {
    buffer.emplace_back(std::move(t));
    if (buffer.size() == capacity)
      processBuffer();
  }

  void done() { processBuffer(); }
};

} // anonymous namespace

void import_command(Database2& db,
                    const std::string& line,
                    const std::function<void (const std::string&, const CompilationCommand&)>& notify)
{
  CompilationCommand cmd;
  import_command(db, line, notify, cmd);
}

void import_commands(Database2& db,
                     std::istream& in,
                     const std::function<void (const std::string&, const CompilationCommand&)>& notify)
{
  std::string line;
  CompilationCommand cmd;

  while (std::getline(in, line) && !line.empty()) {
    clear(cmd);
    import_command(db, line, notify, cmd);
  }
}

void extract_dependencies(Database2& db,
                          const std::function<DependenciesNotifier>& notify) {
  const long long date_import_commands = db.get_timestamp("import-commands");
  const long long date_extract_dependencies = db.get_timestamp("extract-dependencies");
  if (date_extract_dependencies > date_import_commands)
    return;

  const std::vector<bfs::path> default_library_directories = load_default_library_directories();

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

    if (notify)
      notify(cmd, artifacts, dependencies.errors);
  }

  db.set_timestamp("extract-dependencies",
                   std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

bool has_failure(const std::vector<ProcessResult>& processes) {
  return std::any_of(processes.begin(), processes.end(), failed);
}

bool has_failure(const SymbolExtractionStatus& status) {
  return status.linker_script || has_failure(status.processes);
}

void extract_symbols(Database2& db,
                     const std::function<void(const Artifact&, const SymbolExtractionStatus&)>& notify)
{
  BufferedTasks<Artifact> tasks(64, SymbolExtractor(db, notify));

  auto q = db.statement("select id, name, type from artifacts where type not in (\"source\", \"static\")");
  while (q.executeStep()) {
    Artifact artifact;
    artifact.id = q.getColumn(0).getInt64();
    artifact.name = q.getColumn(1).getString();
    artifact.type = q.getColumn(2).getString();
    tasks.push(std::move(artifact));
  }

  tasks.done();
}
