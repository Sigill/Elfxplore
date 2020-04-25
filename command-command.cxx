#include "command-command.hxx"

#include <iostream>
#include <boost/tokenizer.hpp>
#include <termcolor/termcolor.hpp>

#include "Database2.hxx"
#include "utils.hxx"
#include "logger.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

struct CompilationCommand {
  std::string directory;
  std::string executable;
  std::string args;
  std::string output_type;
};

const std::vector<std::string> gcc_commands = {"cc", "c++", "gcc", "g++"};

bool is_cc(const std::string& command) {
  return std::find(gcc_commands.begin(), gcc_commands.end(), command) != gcc_commands.end();
}

using Tokenizer = boost::tokenizer<boost::escaped_list_separator<char>, typename std::string::const_iterator, std::string>;

bool next_token(Tokenizer::iterator& cur, const Tokenizer::iterator& end, std::string& out) {
  if (cur == end)
    return false;

  out = *cur;
  ++cur;
  return true;
}

void process_command(const std::string& line, std::vector<CompilationCommand>& commands) {
  CompilationCommand command;

  Tokenizer tok(line.begin(), line.end(), boost::escaped_list_separator<char>("\\", " \t", "'\""));

  typename Tokenizer::iterator cur_token(tok.begin()), end_token(tok.end());

  const bool valid_line = next_token(cur_token, end_token, command.directory) == true
      && next_token(cur_token, end_token, command.executable) == true
      && cur_token != end_token;

  if (valid_line) {
    command.args = std::string(cur_token.base(), line.end());

    if (is_cc(command.executable)) {
      for (; cur_token != end_token; ++cur_token) {
        if (starts_with(*cur_token, "-o")) {
          if (cur_token->size() == 2) {
            command.output_type = output_type(*cur_token);
          } else {
            ++cur_token;
            if (cur_token != end_token) {
              command.output_type = output_type(*cur_token);
            }
          }
        }
      }
    } else if (command.executable == "ar") {
      command.output_type = "static";
    }
  }

  LOG(info || !valid_line || command.output_type.empty()) << termcolor::green << "Processing command " << termcolor::reset << line;

  if (!valid_line) {
    LOG(always) << termcolor::red << "Error: not enough arguments" << termcolor::reset;
    return;
  }

  if (command.output_type.empty()) {
    LOG(warning) << "Warning: unrecognized output type";
    command.output_type = "unknown";
  } else {
    LOG(debug) << "Output type: " << command.output_type;
  }

  commands.emplace_back(std::move(command));
}

void process_commands(std::istream& in, std::vector<CompilationCommand>& operations) {
  std::string line;
  while (std::getline(in, line) && !line.empty()) {
    process_command(line, operations);
  }
}

} // anonymous namespace

boost::program_options::options_description Command_Command::options()
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

int Command_Command::execute(const std::vector<std::string>& args)
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

  {
    SQLite::Transaction transaction(db.database());

    for(const CompilationCommand& command : commands) {
      db.create_command(command.directory, command.executable, command.args, command.output_type);
    }

    transaction.commit();
  }

  db.optimize();

  return 0;
}
