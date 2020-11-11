#include "Database3.hxx"

#include <fstream>

#include <ansi.hxx>

#include "logger.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "nm.hxx"
#include "progressbar.hxx"

using ansi::style;

namespace {

void log_command(const std::string& line, const CompilationCommand& command) {
  LOG_CTX() << style::green_fg << "Processing command " << style::reset << line;

  if (command.directory.empty() || command.executable.empty() || command.args.empty()) {
    LOG(error) << style::red_fg << "Error: not enough arguments" << style::reset;
    return;
  }

  if (command.output.empty()) {
    LOG(error) << style::red_fg << "Error: no output identified" << style::reset;
    return;
  }

  LOG(debug) << style::blue_fg << "Directory: " << style::reset << command.directory;
  LOG(debug) << style::blue_fg << "Output:    " << style::reset << command.output << " (" << command.output_type << ") " << command.output;
}

void log_dependencies(const CompilationCommand& cmd,
                      const std::vector<Artifact>& dependencies,
                      const std::vector<std::string>& errors) {
  LOG(debug || !errors.empty()) << style::green_fg << "Command #" << cmd.id << style::reset
                               << " " << cmd.directory << " " << cmd.executable << " " << cmd.args;

  for(const std::string& err : errors) {
    LOG(always) << style::red_fg << "Error: " << style::reset << err;
  }

  LOG(trace) << style::blue_fg << ">" << style::reset << " (" << cmd.output_type << ") " << cmd.artifact_id << " " << cmd.output;

  for (const Artifact& dependency : dependencies) {
    LOG(trace) << style::yellow_fg << "<" << style::reset << " (" << dependency.type << ") " << dependency.id << " " << dependency.name;
  }
}

void log_symbols(const Artifact& artifact, const SymbolExtractionStatus& status) {
  LOG(debug || has_failure(status)) << style::green_fg << "Artifact #" << artifact.id << style::reset << " " << artifact.name;

  LOG(error && status.linker_script) << style::red_fg << "Linker scripts are not supported" << style::reset;

  for(const ProcessResult& process : status.processes) {
    LOG(error && failed(process)) << process.command;
    LOG(error && process.code != 0) << "Status: " << style::red_fg << (int)process.code << style::reset;
    LOG(error && !process.err.empty()) << style::red_fg << "stderr: " << style::reset << process.err;
  }
}

class artifacts_stats {
public:
  long long count;
  std::map<std::string, long long> count_by_type;
  explicit artifacts_stats(Database2& db)
    : count(db.count_artifacts()),
      count_by_type(db.count_artifacts_by_type())
  {}
};

std::ostream& operator<<(std::ostream& os, const artifacts_stats& m) {
  os << m.count << " artifacts (";
  bool first = true;
  for(const auto& type : m.count_by_type) {
    if (!first)
      os << ", ";
    first = false;

    os << type.second << " " << type.first;
  }
  os << ")";
  return os;
}

} // anonymous namespace

Database3::Database3(const std::string& storage)
  : Database2(storage)
{}

void Database3::load_commands(const std::vector<std::string>& line_commands,
                              const std::vector<std::string>& compile_commands)
{
  LOG(info) << style::blue_fg << "Loading commands" << style::reset;

  size_t count = 0UL;
  auto log = [&count](const std::string& line, const CompilationCommand& command){
    ++count;
    log_command(line, command);
  };

  for(const std::string& command : line_commands) {
    if (command == "-") {
      import_commands(*this, std::cin, log);
    } else {
      std::ifstream in(command);
      import_commands(*this, in, log);
    }
  }

  for(const std::string& input : compile_commands) {
    std::ifstream in(input);
    import_compile_commands(*this, in, log);
  }

  LOG(info) << style::green_fg << count << " commands imported" << style::reset << " " << artifacts_stats(*this);

  set_timestamp("import-commands",
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

void Database3::load_dependencies()
{
  const long long date_import_commands = get_timestamp("import-commands");
  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  if (date_extract_dependencies > date_import_commands) {
    LOG(debug) << "Dependencies table is up to date";
    return;
  }

  LOG_CTX() << style::blue_fg << "Extracting dependencies" << style::reset;

  DependenciesExtractor e;
  ProgressBar progress("Dependency extraction");
  e.notifyTotalSteps = [&progress](const size_t size){ progress.start(size); };
  e.notifyStep = [&progress](const CompilationCommand& cmd, const std::vector<Artifact>& dependencies, const std::vector<std::string>& errors){
    log_dependencies(cmd, dependencies, errors);
    ++progress;
  };
  e.run(*this);

  LOG(info) << artifacts_stats(*this);
  LOG(info) << count_dependencies() << " dependencies";

  set_timestamp("extract-dependencies",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

void Database3::load_symbols()
{
  load_dependencies();

  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  const long long date_extract_symbols = get_timestamp("extract-symbols");
  if (date_extract_symbols > date_extract_dependencies) {
    LOG(info) << "Symbols table is up to date";
    return;
  }

  LOG_CTX() << style::blue_fg << "Extracting symbols" << style::reset;

  SymbolExtractor e(4);
  ProgressBar progress("Symbol extraction");
  e.notifyTotalSteps = [&progress](const size_t size){ progress.start(size); };
  e.notifyStep = [&progress](const Artifact& artifact, const SymbolExtractionStatus& status){
    log_symbols(artifact, status);
    ++progress;
  };
  e.run(*this);

  LOG(info) << count_symbols() << " symbols (" << count_symbol_references() << " references)";

  set_timestamp("extract-symbols",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}
