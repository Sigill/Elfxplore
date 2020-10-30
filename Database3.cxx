#include "Database3.hxx"

#include <fstream>

#include <termcolor/termcolor.hpp>

#include "logger.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "nm.hxx"
#include "progressbar.hxx"

namespace {

void log_command(const std::string& line, const CompilationCommand& command) {
  LOG(debug || !command.is_complete())
      << termcolor::green << "Processing command " << termcolor::reset << line;

  if (command.directory.empty() || command.executable.empty() || command.args.empty()) {
    LOG(always) << termcolor::red << "Error: not enough arguments" << termcolor::reset;
    return;
  }

  if (command.output.empty()) {
    LOG(always) << termcolor::red << "Error: no output identified" << termcolor::reset;
    return;
  }

  if (command.output_type.empty()) {
    LOG(trace) << "Output type: " << command.output_type;
  }

  LOG(debug) << termcolor::blue << "Directory: " << termcolor::reset << command.directory;
  LOG(debug) << termcolor::blue << "Output: "  << termcolor::reset << "(" << command.output_type << ") " << command.output;
}

void log_dependencies(const CompilationCommand& cmd,
                      const std::vector<Artifact>& dependencies,
                      const std::vector<std::string>& errors) {
  LOG(debug || !errors.empty()) << termcolor::green << "Command #" << cmd.id << termcolor::reset
                               << " " << cmd.directory << " " << cmd.executable << " " << cmd.args;

  for(const std::string& err : errors) {
    LOG(always) << termcolor::red << "Error: " << termcolor::reset << err;
  }

  LOG(trace) << termcolor::blue << ">" << termcolor::reset << " (" << cmd.output_type << ") " << cmd.artifact_id << " " << cmd.output;

  for (const Artifact& dependency : dependencies) {
    LOG(trace) << termcolor::yellow << "<" << termcolor::reset << " (" << dependency.type << ") " << dependency.id << " " << dependency.name;
  }
}

void log_symbols(const Artifact& artifact, const SymbolExtractionStatus& status) {
  LOG(debug || has_failure(status)) << termcolor::green << "Artifact #" << artifact.id << termcolor::reset << " " << artifact.name;

  LOG(error && status.linker_script) << termcolor::red << "Linker scripts are not supported" << termcolor::reset;

  for(const ProcessResult& process : status.processes) {
    LOG(error && failed(process)) << process.command;
    LOG(error && process.code != 0) << "Status: " << termcolor::red << (int)process.code << termcolor::reset;
    LOG(error && !process.err.empty()) << termcolor::red << "stderr: " << termcolor::reset << process.err;
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

  LOG(info) << termcolor::green << count << " commands imported" << termcolor::reset << " " << artifacts_stats(*this);

  set_timestamp("import-commands",
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

void Database3::load_dependencies()
{
  LOG(info) << termcolor::blue << "Extracting dependencies" << termcolor::reset;

  const long long date_import_commands = get_timestamp("import-commands");
  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  if (date_extract_dependencies > date_import_commands) {
    LOG(info) << "Dependencies table is up to date";
    return;
  }

  DependenciesExtractor e;
  ProgressBar progress;
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

  LOG(info) << termcolor::blue << "Extracting symbols" << termcolor::reset;

  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  const long long date_extract_symbols = get_timestamp("extract-symbols");
  if (date_extract_symbols > date_extract_dependencies) {
    LOG(info) << "Symbols table is up to date";
    return;
  }

  SymbolExtractor e(4);
  ProgressBar progress;
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
