#include <iostream>
#include <boost/program_options.hpp>

#include "db-command.hxx"
#include "extract-symbols-command.hxx"
#include "extract-dependencies-command.hxx"
#include "analyse-symbols-command.hxx"
#include "export-dependencies-command.hxx"

namespace bpo = boost::program_options;

void usage(const std::vector<std::string>& commands, const std::vector<std::string>& extra_arg = {}) {
  std::cout << "Usage:";
  for(const std::string& command : commands)
    std::cout << " " << command;
  for(const std::string& arg : extra_arg)
    std::cout << " " << arg;
  std::cout << " [options]" << std::endl;
}

bool is_help(const std::string& command) {
  return command == "help" || command == "-h" || command == "--help";
}

int main(int argc, char** argv)
{
  std::vector<std::string> commands = {argv[0]};

  if (argc == 1) {
    usage(commands);
    return -1;
  }

  commands.emplace_back(argv[1]);
  if (is_help(commands.back())) {
    usage(commands);
    return 0;
  } else if (commands.back() == "db") {
    return db_command(commands, std::vector<std::string>(argv + 2, argv + argc));
  } else if (commands.back() == "extract-symbols") {
    return extract_symbols_command(commands, std::vector<std::string>(argv + 2, argv + argc));
  } else if (commands.back() == "extract-dependencies") {
    extract_dependencies_command(commands, std::vector<std::string>(argv + 2, argv + argc));
  } else if (commands.back() == "analyse-symbols") {
    analyse_symbols_command(commands, std::vector<std::string>(argv + 2, argv + argc));
  } else if (commands.back() == "export-dependencies") {
    export_dependencies_command(commands, std::vector<std::string>(argv + 2, argv + argc));
  }
}
