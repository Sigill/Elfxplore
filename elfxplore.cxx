#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>

#include "tasks/command-task.hxx"
#include "tasks/db-task.hxx"
#include "tasks/extract-task.hxx"
#include "tasks/analyse-task.hxx"
#include "tasks/dependencies-task.hxx"
#include "tasks/artifacts-task.hxx"

namespace {
#define COMMAND_FACTORY(T) [](const std::vector<std::string>& args){ return std::make_unique<T>(args); }

using CommandFactory = std::function<std::unique_ptr<Task>(const std::vector<std::string>&)>;
const std::vector<std::pair<std::string, CommandFactory>> commands = {
    {"db"                  , COMMAND_FACTORY(DB_Task)},
    {"command"             , COMMAND_FACTORY(Command_Task)},
    {"extract"             , COMMAND_FACTORY(Extract_Task)},
    {"dependencies"        , COMMAND_FACTORY(Dependencies_Task)},
    {"artifacts"           , COMMAND_FACTORY(Artifacts_Task)},
    {"analyse"             , COMMAND_FACTORY(Analyse_Task)},
};

std::unique_ptr<Task> get_command(const std::vector<std::string>& args) {
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

  std::unique_ptr<Task> cmd = get_command(args);
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
