#include "command-task.hxx"

#include <iostream>
#include <boost/tokenizer.hpp>
#include <termcolor/termcolor.hpp>

#include "Database2.hxx"
#include "utils.hxx"
#include "logger.hxx"
#include "command-utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

void process_command(const std::string& line, std::vector<CompilationCommand>& commands) {
  CompilationCommand command;
  parse_command(line, command);

  LOG(info || !command.is_complete())
      << termcolor::green << "Processing command " << termcolor::reset << line;

  if (command.executable.empty() || command.executable.empty() || command.args.empty()) {
    LOG(always) << termcolor::red << "Error: not enough arguments" << termcolor::reset;
    return;
  }

  if (command.output.empty()) {
    LOG(always) << termcolor::red << "Error: no output indentified" << termcolor::reset;
    return;
  }

  if (command.output_type.empty()) {
    LOG(warning) << "Warning: unrecognized output type";
    command.output_type = "unknown";
  } else {
    LOG(debug) << "Output type: " << command.output_type;
  }

  LOG(info) << termcolor::blue << "Directory: " << termcolor::reset << command.directory;
  LOG(info) << termcolor::blue << "Output: "  << termcolor::reset << "(" << command.output_type << ") " << command.output;

  commands.emplace_back(std::move(command));
}

void process_commands(std::istream& in, std::vector<CompilationCommand>& operations) {
  std::string line;
  while (std::getline(in, line) && !line.empty()) {
    process_command(line, operations);
  }
}

} // anonymous namespace

boost::program_options::options_description Command_Task::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("import", bpo::value<std::vector<std::string>>()->multitoken()->implicit_value({"-"}, "-"),
       "Command lines to import.\n"
       "Use - to read from cin (default).\n"
       "Use @path/to/file to read from a file.")
      ;

  return opt;
}

int Command_Task::execute(const std::vector<std::string>& args)
{
  bpo::positional_options_description p;
  p.add("import", -1);

  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(options()).positional(p).run(), vm);

    if (vm.count("help")) {
      usage(std::cout);
      return 0;
    }

    bpo::notify(vm);
  } catch(bpo::error &err) {
    std::cerr << err.what() << std::endl;
    return -1;
  }

  const std::vector<std::string> raw_commands = vm["import"].as<std::vector<std::string>>();
  std::vector<CompilationCommand> commands;

  for(const std::string& command : raw_commands) {
    if (command == "-") {
      process_commands(std::cin, commands);
    } else if (command[0] == '@') {
      const bfs::path lst = expand_path(command.substr(1)); // Might be @~/...
      std::ifstream in(lst.string());
      process_commands(in, commands);
    } else {
      process_command(command, commands);
    }
  }

  Database2 db(vm["db"].as<std::string>());

  SQLite::Transaction transaction(db.database());

  for(const CompilationCommand& command : commands) {
    const long long command_id = db.create_command(command.directory, command.executable, command.args);

    const bfs::path output = expand_path(command.output, command.directory);

    db.create_artifact(output.string(), command.output_type, command_id);
  }

  LOGGER << termcolor::blue << "Status" << termcolor::reset;
  LOGGER << db.count_artifacts() << " artifacts";
  for(const auto& type : db.count_artifacts_by_type()) {
    LOGGER << "\t" << type.second << " " << type.first;
  }

  if (!dryrun()) {
    transaction.commit();
    db.optimize();
  } else {
    LOG(always) << "Dry-run, aborting transaction";
  }

  return 0;
}
