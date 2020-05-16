#include "Database3.hxx"

#include <termcolor/termcolor.hpp>

#include "logger.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "nm.hxx"

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
  extract_dependencies(*this, log_dependencies);
}

void Database3::load_symbols()
{
  extract_symbols(*this, log_symbols);

  LOGGER << termcolor::blue << "Status" << termcolor::reset;
  std::cout << count_symbols() << " symbols" << std::endl;
  std::cout << count_symbol_references() << " symbol references" << std::endl;
}
