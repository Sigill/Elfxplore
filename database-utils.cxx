#include "database-utils.hxx"

#include <fstream>

#include <boost/filesystem/path.hpp>

#include "Database2.hxx"
#include "command-utils.hxx"
#include "utils.hxx"

namespace bfs = boost::filesystem;

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
