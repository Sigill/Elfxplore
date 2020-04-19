#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "command.hxx"
#include "db-command.hxx"
#include "extract-symbols-command.hxx"
#include "extract-dependencies-command.hxx"
#include "analyse-command.hxx"
#include "dependencies-command.hxx"
#include "artifacts-command.hxx"

namespace {
#define COMMAND_FACTORY(T) [](const std::vector<std::string>& args){ return std::make_unique<T>(args); }

using CommandFactory = std::function<std::unique_ptr<Command>(const std::vector<std::string>&)>;
const std::vector<std::pair<std::string, CommandFactory>> commands = {
    {"db"                  , COMMAND_FACTORY(DB_Command)},
    {"extract-dependencies", COMMAND_FACTORY(Extract_Dependencies_Command)},
    {"extract-symbols"     , COMMAND_FACTORY(Extract_Symbols_Command)},
    {"dependencies"        , COMMAND_FACTORY(Dependencies_Command)},
    {"artifacts"           , COMMAND_FACTORY(ArtifactsCommand)},
    {"analyse"             , COMMAND_FACTORY(Analyse_Command)},
};

std::unique_ptr<Command> get_command(const std::vector<std::string>& args) {
  auto factory = std::find_if(commands.begin(),
                              commands.end(),
                              [&name=args.back()](const std::pair<std::string, CommandFactory>& cmd){ return cmd.first == name; });
  if (factory == commands.end())
    return nullptr;
  else
    return factory->second(args);
}

void usage(const std::vector<std::string>& args) {
  std::cout << "Usage:";
  for(const std::string& arg : args)
    std::cout << " " << arg;
  std::cout << " [help|-h|--help] <command> [command args]\n"
            << "Available commands:\n";
  for(const auto& cmd : commands)
    std::cout << "  " << cmd.first << "\n";
}

bool is_help(const std::string& arg) {
  return arg == "help" || arg == "-h" || arg == "--help";
}
} // anonymous namespace

int main(int argc, char** argv)
{
  std::vector<std::string> args = {argv[0]};

  if (argc == 1) {
    usage(args);
    return -1;
  }

  size_t i = 1;
  bool print_help = is_help(argv[i]);

  if (print_help && argc == 2) {
    usage(args);
    return 0;
  }

  if (print_help) {
    ++i;
  }

  args.emplace_back(argv[i]);

  std::unique_ptr<Command> cmd = get_command(args);
  if (cmd) {
    if (print_help) {
      cmd->usage(std::cout);
    } else {
      return cmd->execute(std::vector<std::string>(argv + 2, argv + argc));
    }
  } else {
    std::cerr << "Unknown command: " << args.back() << std::endl;
    args.pop_back();
    usage(args);
  }
}
