#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>
#include <fstream>
#include <chrono>

#include <boost/program_options.hpp>

#include <termcolor/termcolor.hpp>

#include <SQLiteCpp/Transaction.h>

#include "logger.hxx"
#include "Database3.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "utils.hxx"

#include "tasks/command-task.hxx"
#include "tasks/db-task.hxx"
#include "tasks/extract-task.hxx"
#include "tasks/analyse-task.hxx"
#include "tasks/dependencies-task.hxx"
#include "tasks/artifacts-task.hxx"

namespace bpo = boost::program_options;

namespace {
#define COMMAND_FACTORY(T) [](){ return std::make_unique<T>(); }

using CommandFactory = std::function<std::unique_ptr<Task>()>;
const std::vector<std::pair<std::string, CommandFactory>> commands = {
    {"db"                  , COMMAND_FACTORY(DB_Task)},
//    {"command"             , COMMAND_FACTORY(Command_Task)},
    {"extract"             , COMMAND_FACTORY(Extract_Task)},
    {"dependencies"        , COMMAND_FACTORY(Dependencies_Task)},
    {"artifacts"           , COMMAND_FACTORY(Artifacts_Task)},
    {"analyse"             , COMMAND_FACTORY(Analyse_Task)},
};

std::unique_ptr<Task> get_task(const std::string& command_arg) {
  auto factory = std::find_if(commands.begin(),
                              commands.end(),
                              [&command_arg](const std::pair<std::string, CommandFactory>& cmd){ return cmd.first == command_arg; });
  if (factory == commands.end())
    return nullptr;
  else
    return factory->second();
}

void usage(std::ostream& out,
           const std::string& argv0,
           const bpo::options_description &base_options) {
  out << "Usage: " << argv0 << " command [options]\n";
  out << base_options;
  out << "Available commands:\n";
  for(const auto& cmd : commands)
    out << "  " << cmd.first << "\n";
}

void usage(std::ostream& out,
           const std::vector<std::string>& args,
           const bpo::options_description &base_options,
           const bpo::options_description &task_options) {
  out << "Usage:";
  for(const std::string& arg : args)
    out << ' ' << arg;
  out << " command [options]\n";
  out << base_options;
  out << task_options;
}

void log_command(const std::string& line, const CompilationCommand& command) {
  LOG(info || !command.is_complete())
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
    LOG(debug) << "Output type: " << command.output_type;
  }

  LOG(info) << termcolor::blue << "Directory: " << termcolor::reset << command.directory;
  LOG(info) << termcolor::blue << "Output: "  << termcolor::reset << "(" << command.output_type << ") " << command.output;
}

} // anonymous namespace

namespace bpo = boost::program_options;

namespace logger {

void validate(boost::any& v,
              const std::vector<std::string>& values,
              ::logger::severity_level* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  const std::string& s = bpo::validators::get_single_string(values);

  if (s == "trace")
    v = boost::any(logger::trace);
  else if (s == "debug")
    v = boost::any(logger::debug);
  else if (s == "info")
    v = boost::any(logger::info);
  else if (s == "warning")
    v = boost::any(logger::warning);
  else if (s == "error")
    v = boost::any(logger::error);
  else if (s == "fatal")
    v = boost::any(logger::fatal);
  else
    throw bpo::invalid_option_value(s);
}

} // namespace logger

int main(int argc, char** argv)
{
  std::vector<std::string> args = {argv[0]};

  bool dryrun = false;
  std::string storage;
  std::vector<std::string> compilation_databases;
  std::vector<std::string> compilation_commands;

  bpo::options_description opts("Common options");
  opts.add_options()
      ("help,h",
       "Produce help message.")
      ("verbose,v",
       bpo::value<logger::severity_level>(&::logger::_severity_level)->default_value(logger::fatal, "fatal"),
       "Verbosity level (trace, debug, info, warning, error, fatal).")
      ("dry-run,n",
       bpo::bool_switch(&dryrun),
       "Do not write anything to the database.")
      ("compilation-database",
       bpo::value<std::vector<std::string>>(&compilation_databases)->multitoken()->value_name("files")->implicit_value({"-"}, "-"),
       "List of the commands used to generate the project. "
       "This file can also include link commands used to generate libraries & executables.")
      ("compilation-commands",
       bpo::value<std::vector<std::string>>(&compilation_commands)->multitoken()->value_name("files")->implicit_value({"-"}, "-"),
       "List of the commands used to generate the project. "
       "This file can also include link commands used to generate libraries & executables.")
      ("storage",
       bpo::value<std::string>(&storage)->value_name("file")->default_value(":memory:"),
       "SQLite database used as backend. If not specified, a temporary in-memory database is used.")
      ;

  if (argc == 1) {
    usage(std::cerr, args.front(), opts);
    return -1;
  }

  const bpo::parsed_options recognized_args = bpo::command_line_parser(argc, argv).options(opts).allow_unregistered().run();
  bpo::variables_map vm;
  bpo::store(recognized_args, vm);
  vm.notify();

  const bool print_help = vm.count("help") > 0;

  std::vector<std::string> unrecognized_args = bpo::collect_unrecognized(recognized_args.options, bpo::include_positional);

  if (unrecognized_args.empty()) {
    usage(std::cout, args.front(), opts);
    return 0;
  }

  const std::string task_name = unrecognized_args.front();
  unrecognized_args.erase(unrecognized_args.begin());

  std::unique_ptr<Task> task = get_task(task_name);
  if (!task) {
    std::cerr << "Unknown command: " << task_name << std::endl;
    usage(std::cerr, args.front(), opts);
    return -1;
  }

  args.push_back(task_name);

  if (print_help) {
    usage(std::cout, args, opts, task->options());
    return 0;
  }

  auto db = std::make_shared<Database3>(storage);

  SQLite::Transaction transaction(db->database());

  for(const std::string& command : compilation_commands) {
    if (command == "-") {
      import_commands(*db, std::cin, log_command);
    } else {
      std::ifstream in(command);
      import_commands(*db, in, log_command);
    }
  }

  LOGGER << termcolor::blue << "Status" << termcolor::reset;
  LOGGER << db->count_artifacts() << " artifacts";
  for(const auto& type : db->count_artifacts_by_type()) {
    LOGGER << "\t" << type.second << " " << type.first;
  }

  db->set_timestamp("import-commands",
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());

  task->set_dryrun(dryrun);
  task->set_database(db);

  const int status = task->execute(unrecognized_args);

  if (status == TaskStatus::SUCCESS) {
    if (dryrun) {
      LOG(always) << "Dry-run, aborting transaction";
    } else {
      transaction.commit();
      db->optimize();
    }
  }

  if (status == WRONG_ARGS) {
    usage(std::cerr, args, opts, task->options());
  }

  return status;
}
