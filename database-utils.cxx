#include "database-utils.hxx"

#include <fstream>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/process.hpp>

#include "Database2.hxx"
#include "command-utils.hxx"
#include "utils.hxx"
#include "logger.hxx"

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
                          const std::function<DependenciesCallback>& notify) {
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
}
