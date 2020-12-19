#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>

#include <boost/program_options.hpp>

#include <SQLiteCpp/Transaction.h>

#include "ansi.hxx"
#include "logger.hxx"
#include "Database3.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"
#include "utils.hxx"

#include "tasks/import-command-task.hxx"
#include "tasks/db-task.hxx"
#include "tasks/extract-task.hxx"
#include "tasks/analyse-task.hxx"
#include "tasks/dependencies-task.hxx"
#include "tasks/artifacts-task.hxx"

namespace bpo = boost::program_options;
using ::CTXLogger::severity_level;
using ansi::style;

namespace {
#define COMMAND_FACTORY(T) [](){ return std::make_unique<T>(); }

using CommandFactory = std::function<std::unique_ptr<Task>()>;
const std::vector<std::pair<std::string, CommandFactory>> commands = {
    {"db"                  , COMMAND_FACTORY(DB_Task)},
    {"import-command"      , COMMAND_FACTORY(ImportCommand_Task)},
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
           const bpo::options_description& base_options) {
  out << "Usage: " << argv0 << " command [options]\n";
  out << base_options;
  out << "Available commands:\n";
  for(const auto& cmd : commands)
    out << "  " << cmd.first << "\n";
}

void usage(std::ostream& out,
           const std::string& argv0,
           const std::string& task_name,
           const bpo::options_description& base_options,
           const bpo::options_description& task_options) {
  out << "Usage: " << argv0 << ' ' << task_name << " [options]\n";
  out << base_options;
  out << task_options;
}

} // anonymous namespace

namespace CTXLogger {

void validate(boost::any& v,
              const std::vector<std::string>& values,
              ::CTXLogger::severity_level* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  const std::string& s = bpo::validators::get_single_string(values);

  if (s == "trace")
    v = boost::any(severity_level::trace);
  else if (s == "debug")
    v = boost::any(severity_level::debug);
  else if (s == "info")
    v = boost::any(severity_level::info);
  else if (s == "warning")
    v = boost::any(severity_level::warning);
  else if (s == "error")
    v = boost::any(severity_level::error);
  else if (s == "fatal")
    v = boost::any(severity_level::fatal);
  else
    throw bpo::invalid_option_value(s);
}

} // namespace CTXLogger

int main(int argc, char** argv)
{
  CTXLogger::ansi_support = ansi::is_atty(std::cerr);

  bool help = false;
  bool dryrun = false;
  std::string storage;

  bpo::options_description base_options {"Common options"};
  base_options.add_options()
      ("help,h",
       bpo::bool_switch(&help),
       "Produce help message.")
      ("verbose,v",
       bpo::value<CTXLogger::severity_level>(&CTXLogger::_severity_level)->default_value(CTXLogger::severity_level::warning, "warning"),
       "Verbosity level (trace, debug, info, warning, error, fatal).")
      ("dry-run,n",
       bpo::bool_switch(&dryrun),
       "Do not write anything to the database.")
      ("storage",
       bpo::value<std::string>(&storage)->value_name("file")->default_value(":memory:"),
       "SQLite database used as backend. If not specified, a temporary in-memory database is used.")
      ;

  if (argc == 1) {
    usage(std::cerr, argv[0], base_options);
    return -1;
  }

  try {
    const bpo::parsed_options recognized_args = bpo::command_line_parser(argc, argv)
                                                .style(bpo::command_line_style::default_style & ~bpo::command_line_style::allow_guessing)
                                                .options(base_options)
                                                .allow_unregistered()
                                                .run();
    bpo::variables_map vm;
    bpo::store(recognized_args, vm);
    vm.notify();

    std::vector<std::string> args = bpo::collect_unrecognized(recognized_args.options, bpo::include_positional);

    if (args.empty()) {
      if (help) {
        usage(std::cout, argv[0], base_options);
        return 0;
      } else {
        throw bpo::error("No command specified");
      }
    }

    std::string task_name = args.front();
    args.erase(args.begin());
    std::unique_ptr<Task> task = get_task(task_name);

    if (!task) {
      throw bpo::error("Unknown command \"" + task_name + "\"");
    }

    if (help) {
      usage(std::cout, argv[0], task_name, base_options, task->options());
      return 0;
    }

    task->parse_args(args);

    Database3 db(storage);

    SQLite::Transaction transaction(db.database());
    bool commit = !dryrun;

    try {
      task->execute(db);
      commit &= true;
    }
    catch (const std::exception& ex) { LOG_EX(fatal, ex); }
    catch (...) { LOG(fatal) << "Unknown exception"; }

    if (dryrun) {
      LOG(info) << "Dry-run, aborting transaction";
    } else {
      if (commit) {
        transaction.commit();
        db.optimize();
      } else {
        LOG(info) << "Aborting";
      }
    }
  }
  catch (const std::exception& ex) {
    LOG_EX(fatal, ex);
    return -1;
  }
}
