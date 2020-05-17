#include "Database3.hxx"

#include <termcolor/termcolor.hpp>

#include "logger.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "nm.hxx"
#include "progressbar.hxx"

namespace {

void log_dependencies(const CompilationCommand& cmd,
                      const std::vector<Artifact>& dependencies,
                      const std::vector<std::string>& errors) {
  LOG(info || !errors.empty()) << termcolor::green << "Command #" << cmd.id << termcolor::reset
                               << " " << cmd.directory << " " << cmd.executable << " " << cmd.args;

  for(const std::string& err : errors) {
    LOG(always) << termcolor::red << "Error: " << termcolor::reset << err;
  }

  LOG(debug) << termcolor::blue << ">" << termcolor::reset << " (" << cmd.output_type << ") " << cmd.artifact_id << " " << cmd.output;

  for (const Artifact& dependency : dependencies) {
    LOG(debug) << termcolor::yellow << "<" << termcolor::reset << " (" << dependency.type << ") " << dependency.id << " " << dependency.name;
  }
}

void log_symbols(const Artifact& artifact, const SymbolExtractionStatus& status) {
  LOG(info || has_failure(status)) << termcolor::green << "Artifact #" << artifact.id << termcolor::reset << " " << artifact.name;

  LOG(always && status.linker_script) << termcolor::red << "Linker scripts are not supported" << termcolor::reset;

  for(const ProcessResult& process : status.processes) {
    LOG(always && failed(process)) << process.command;
    LOG(always && process.code != 0) << "Status: " << termcolor::red << (int)process.code << termcolor::reset;
    LOG(always && !process.err.empty()) << termcolor::red << "stderr: " << termcolor::reset << process.err;
  }
}

} // anonymous namespace

Database3::Database3(const std::string& storage)
  : Database2(storage)
{}

void Database3::load_dependencies()
{
  const long long date_import_commands = get_timestamp("import-commands");
  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  if (date_extract_dependencies > date_import_commands) {
    LOG(always) << "Dependencies table is up to date";
    return;
  }

  LOG(always) << "Extracting dependencies";

  DependenciesExtractor e;
  ProgressBar progress;
  e.notifyTotalSteps = [&progress](const size_t size){ progress.start(size); };
  e.notifyStep = [&progress](const CompilationCommand& cmd, const std::vector<Artifact>& dependencies, const std::vector<std::string>& errors){
    log_dependencies(cmd, dependencies, errors);
    ++progress;
  };
  e.run(*this);

  LOGGER << termcolor::blue << "Status" << termcolor::reset;
  LOGGER << count_artifacts() << " artifacts";
  for(const auto& type : count_artifacts_by_type()) {
    LOGGER << "\t" << type.second << " " << type.first;
  }
  LOGGER << count_dependencies() << " dependencies";

  set_timestamp("extract-dependencies",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

void Database3::load_symbols()
{
  load_dependencies();

  const long long date_extract_dependencies = get_timestamp("extract-dependencies");
  const long long date_extract_symbols = get_timestamp("extract-symbols");
  if (date_extract_symbols > date_extract_dependencies) {
    LOG(always) << "Symbols table is up to date";
    return;
  }

  LOG(always) << "Extracting symbols";

  SymbolExtractor e;
  ProgressBar progress;
  e.notifyTotalSteps = [&progress](const size_t size){ progress.start(size); };
  e.notifyStep = [&progress](const Artifact& artifact, const SymbolExtractionStatus& status){
    log_symbols(artifact, status);
    ++progress;
  };
  e.run(*this);

  LOGGER << termcolor::blue << "Status" << termcolor::reset;
  std::cout << count_symbols() << " symbols" << std::endl;
  std::cout << count_symbol_references() << " symbol references" << std::endl;

  set_timestamp("extract-symbols",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}
