#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>

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

bool is_stdin(const std::string& s) { return s == "-"; }

class InputFiles : public std::vector<std::string> {
public:
  using std::vector<std::string>::vector;

  bool contain_stdin() const {
    return std::any_of(cbegin(), cend(), is_stdin);
  }
};

class invalid_option_file_not_found : public bpo::error_with_option_name {
public:
  explicit invalid_option_file_not_found(const std::string& bad_value)
    : bpo::error_with_option_name("argument ('%value%') is an invalid path for option '%canonical_option%'")
  {
    set_substitute_default("value", "argument ('%value%')", "(empty string)");
    set_substitute("value", bad_value);
  }
};

class invalid_option_duplicated_value : public bpo::error_with_option_name {
public:
  explicit invalid_option_duplicated_value(const std::string& bad_value)
    : bpo::error_with_option_name("argument ('%value%') is specified multiple times for option '%canonical_option%'")
  {
    set_substitute_default("value", "argument ('%value%')", "(empty string)");
    set_substitute("value", bad_value);
  }
};

void validate(boost::any& v,
              const std::vector<std::string>& values,
              InputFiles* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  // Check unicity of values.
  std::vector<std::string> value_cpy = values;
  std::sort(value_cpy.begin(), value_cpy.end());
  auto it = std::adjacent_find(value_cpy.begin(), value_cpy.end());
  if (it != value_cpy.end())
    throw invalid_option_duplicated_value(*it);

  for(const std::string& value : values) {
    if (is_stdin(value))
      continue;
    if (!std::filesystem::is_regular_file(value))
      throw invalid_option_file_not_found(value);
  }

  v = boost::any(InputFiles(values.begin(), values.end()));
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
  bool help = false;
  bool dryrun = false;
  std::string storage;
  InputFiles compile_commands;
  InputFiles line_commands;

  bpo::options_description base_options {"Common options"};
  base_options.add_options()
      ("help,h",
       bpo::bool_switch(&help),
       "Produce help message.")
      ("verbose,v",
       bpo::value<logger::severity_level>(&::logger::_severity_level)->default_value(logger::fatal, "fatal"),
       "Verbosity level (trace, debug, info, warning, error, fatal).")
      ("dry-run,n",
       bpo::bool_switch(&dryrun),
       "Do not write anything to the database.")
      ("compile-commands",
       bpo::value<InputFiles>(&compile_commands)->multitoken()->value_name("files")->implicit_value({"-"}, "-"),
       "List of the commands used to generate the project. "
       "This file can also include link commands used to generate libraries & executables.")
      ("commands",
       bpo::value<InputFiles>(&line_commands)->multitoken()->value_name("files")->implicit_value({"-"}, "-"),
       "List of the commands used to generate the project. "
       "This file can also include link commands used to generate libraries & executables.")
      ("storage",
       bpo::value<std::string>(&storage)->value_name("file")->default_value(":memory:"),
       "SQLite database used as backend. If not specified, a temporary in-memory database is used.")
      ;

  if (argc == 1) {
    usage(std::cerr, argv[0], base_options);
    return -1;
  }

  std::string task_name;
  std::unique_ptr<Task> task;

  std::vector<std::string> args = {&argv[1], &argv[argc]};

  try {
    const bpo::parsed_options recognized_args = bpo::command_line_parser(args)
                                                .style(bpo::command_line_style::default_style & ~bpo::command_line_style::allow_guessing)
                                                .options(base_options)
                                                .allow_unregistered()
                                                .run();
    bpo::variables_map vm;
    bpo::store(recognized_args, vm);
    vm.notify();

    if (line_commands.contain_stdin() && compile_commands.contain_stdin()) {
      throw bpo::error("argument '-' (aka stdin) cannot be used multiple times");
    }

    args = bpo::collect_unrecognized(recognized_args.options, bpo::include_positional);

    if (!args.empty()) {
      task_name = args.front();
      task = get_task(task_name);

      if (!task) {
        throw bpo::error("unknown task \"" + task_name + "\"");
      }
    }

    if (help) {
      if (!task)
        usage(std::cerr, argv[0], base_options);
      else
        usage(std::cerr, argv[0], task_name, base_options, task->options());

      return 0;
    }

    if (task)
      task->parse_args(args);

    Database3 db(storage);

    SQLite::Transaction transaction(db.database());
    bool commit = !dryrun;

    if (!line_commands.empty() || !compile_commands.empty())
    {
      LOG(info) << termcolor::blue << "Loading commands" << termcolor::reset;

      db.load_commands(line_commands, compile_commands);
    }

    if (task) {
      const int status = task->execute(db);
      commit &= (status == TaskStatus::SUCCESS);
    }

    if (dryrun) {
      LOG(always) << "Dry-run, aborting transaction";
    } else if (commit) {
      transaction.commit();
      db.optimize();
    }
  } catch (const bpo::error& ex) {
    std::cerr << ex.what() << std::endl;
    return -1;
  }
}
