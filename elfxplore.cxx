#include <iostream>
#include <vector>
#include <memory>
#include <boost/program_options.hpp>

#include "command.hxx"
#include "db-command.hxx"
#include "extract-symbols-command.hxx"
#include "extract-dependencies-command.hxx"
#include "analyse-command.hxx"
#include "dependencies-command.hxx"
#include "artifacts-command.hxx"

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

std::unique_ptr<Command> factory(const std::vector<std::string>& command) {
  if (command.back() == "db") {
    return std::make_unique<DB_Command>(command);
  } else if (command.back() == "extract-dependencies") {
    return std::make_unique<Extract_Dependencies_Command>(command);
  } else if (command.back() == "extract-symbols") {
    return std::make_unique<Extract_Symbols_Command>(command);
  } else if (command.back() == "analyse") {
    return std::make_unique<Analyse_Command>(command);
  } else if (command.back() == "dependencies") {
    return std::make_unique<Dependencies_Command>(command);
  } else if (command.back() == "artifacts") {
    return std::make_unique<ArtifactsCommand>(command);
  }

  return nullptr;
}

int main(int argc, char** argv)
{
  std::vector<std::string> commands = {argv[0]};

  if (argc == 1) {
    usage(commands);
    return -1;
  }

  size_t i = 1;
  bool print_help = is_help(argv[i]);

  if (print_help && argc == 2) {
    usage(commands);
    return 0;
  }

  if (print_help) {
    ++i;
  }

  commands.emplace_back(argv[i]);

  std::unique_ptr<Command> cmd = factory(commands);
  if (cmd) {
    if (print_help) {
      cmd->usage(std::cout);
    } else {
      return cmd->execute(std::vector<std::string>(argv + 2, argv + argc));
    }
  } else {
    std::cerr << "Unknown command: " << commands.back() << std::endl;
    commands.pop_back();
    usage(commands);
  }
}
