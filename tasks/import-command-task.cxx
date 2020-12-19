#include "import-command-task.hxx"

#include <fstream>
#include <iostream>
#include <utility>
#include <filesystem>

#include <ansi.hxx>

#include <SQLiteCpp/Transaction.h>

#include "Database3.hxx"
#include "utils.hxx"
#include "logger.hxx"
#include "command-utils.hxx"
#include "database-utils.hxx"

namespace fs = std::filesystem;
namespace bpo = boost::program_options;
using ansi::style;

namespace {

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

struct pretty_input_name {
  const std::string& name;
  pretty_input_name(const std::string& name) : name(name) {}
};

std::ostream& operator<<(std::ostream& os, const pretty_input_name& input) {
  if (input.name == "-")
    os << "stdin";
  else
    os << input.name;
  return os;
}

} // anonymous namespace

boost::program_options::options_description ImportCommand_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("json", bpo::value<InputFiles>()->multitoken()->value_name("file")->implicit_value({"-"}, "-")->default_value({}, ""),
       "Specify that input files are in the JSON compilation database format.\n"
       "Use - to read from the standard input (default).")
      ("list", bpo::value<InputFiles>()->multitoken()->value_name("file")->implicit_value({"-"}, "-")->default_value({}, ""),
       "Specify that input files are in text format (one command per line).\n"
       "Use - to read from the standard input (default).")
      ;

  return opt;
}

void ImportCommand_Task::parse_args(const std::vector<std::string>& args)
{
  bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
  bpo::notify(vm);

  if (/*vm.count("json") > 0 &&*/ vm["json"].as<InputFiles>().contain_stdin()
      /*&& vm.count("list") > 0*/ && vm["list"].as<InputFiles>().contain_stdin()) {
    throw bpo::error("argument '-' (aka stdin) cannot be used multiple times");
  }
}

void ImportCommand_Task::execute(Database3& db)
{
  size_t count = 0UL;
  auto on_command = [&count, &db](size_t item, const std::string& line, const CompilationCommand& command) {
    LOG_CTX() << style::green_fg << "Command #" << item << ": " << style::reset << line;

    if (command.directory.empty())
      throw std::runtime_error("Invalid command: directory could not be identified");

    if (command.executable.empty())
      throw std::runtime_error("Invalid command: executable could not be identified");

    if (command.output.empty())
      throw std::runtime_error("Invalid commant: output could not be identified");

    db.import_command(command);
    ++count;

    LOG(debug) << style::blue_fg << "Directory: " << style::reset << command.directory;
    LOG(debug) << style::blue_fg << "Output:    " << style::reset << command.output << " (" << command.output_type << ")";
  };

  for(const auto& src : vm["json"].as<InputFiles>()) {
    LOG_CTX_FLUSH(info) << "Importing json compilation database from " << pretty_input_name(src);

    count = 0UL;

    if (src == "-") {
      parse_compile_commands(std::cin, on_command);
    } else {
      std::ifstream in(src);
      parse_compile_commands(in, on_command);
    }

    LOG(info) << count << " commands imported";
  }

  for(const auto& src : vm["list"].as<InputFiles>()) {
    LOG_CTX_FLUSH(info) << "Importing list of commands from " << pretty_input_name(src);

    count = 0UL;

    if (src == "-") {
      parse_commands(std::cin, on_command);
    } else {
      std::ifstream in(src);
      parse_commands(in, on_command);
    }

    LOG(info) << count << " commands imported";
  }

  db.set_timestamp("import-commands", std::chrono::high_resolution_clock::now());
}
