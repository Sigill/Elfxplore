#include "command-utils.hxx"

#include <sstream>
#include <algorithm>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <shellwords/shellwords.hxx>
#include <instrmt/instrmt.hxx>

#include "utils.hxx"
#include "Database2.hxx"

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

namespace {

void parse_cc_args(shellwords::shell_splitter& splitter, CompilationCommand& command) {
  while(splitter.read_next()) {
    const std::string& arg = splitter.arg();
    if (starts_with(arg, "-o")) {
      if (arg.size() > 2) {
        command.output = arg.substr(2);
      } else {
        if (splitter.read_next())
          command.output = splitter.arg();
      }
    }
  }
}

void parse_ar_args(shellwords::shell_splitter& splitter, CompilationCommand& command) {
  while(splitter.read_next()) {
    const std::string& arg = splitter.arg();
    if (ends_with(arg, ".a") && command.output.empty())
      command.output = arg;
  }
}

const std::vector<std::string> gcc_commands = {"cc", "c++", "gcc", "g++"};

void clear(CompilationCommand& cmd)
{
  cmd.id = -1;
  cmd.directory.clear();
  cmd.executable.clear();
  cmd.args.clear();
  cmd.output.clear();
  cmd.output_type.clear();
}

} // anonymous namespace

bool is_cc(const std::string& command) {
  return std::any_of(gcc_commands.begin(), gcc_commands.end(), [&command](const std::string& suffix){
    return ends_with(command, suffix);
  });
}

bool is_ar(const std::string& command) {
  return ends_with(command, "ar");
}

void parse_command(const std::string& line, CompilationCommand& command, const int options)
{
  shellwords::shell_splitter splitter(line.begin(), line.end());

  if ((options & parse_command_options::with_directory) && splitter.read_next()) {
    command.directory = splitter.arg();
  }

  if (splitter.read_next()) {
    command.executable = splitter.arg();
  }

  command.args = std::string(splitter.suffix(), line.end());

  const std::string executable = fs::path(command.executable).filename();

  if (is_cc(executable)) {
    parse_cc_args(splitter, command);
  } else if (is_ar(executable)) {
    parse_ar_args(splitter, command);
  }

  if (!command.output.empty()) {
    if (options & parse_command_options::expand_path) {
      command.output = expand_path(command.output, command.directory);
    }
    command.output_type = get_output_type(command.output);
  }
}

namespace {

const std::vector<std::string> ignored_single_args = {"-D", "-w", "-W", "-O", "-m", "-g", "-f", "-MD", "-c",
                                                      "-std", "-rdynamic", "-shared", "-pipe",
                                                      "-ansi", "-pedantic"};
const std::vector<std::string> ignored_double_args = {"-MT", "-MF"};

bool is_arg(const std::string& arg, const std::vector<std::string>& prefixes) {
  return std::any_of(prefixes.begin(), prefixes.end(), [&arg](const std::string& prefix){ return starts_with(arg, prefix); });
}

bool consume_arg(const std::string& arg, const std::string& prefix, size_t& i) {
  if (starts_with(arg, prefix)) {
    if (arg == prefix)
      ++i;
    return true;
  }

  return false;
}

std::string get_arg(const std::vector<std::string>& args, size_t& i) {
  if (args[i].size() == 2) {
    return args[++i];
  } else {
    return args[i].substr(2);
  }
}

bool locate_library(const std::string& name, const std::vector<fs::path>& directories, std::string& out) {
  for(const fs::path& dir : directories) {
    const fs::path candidate = dir / name;
    if (fs::exists(candidate)) {
      out = fs::canonical(candidate);
      return true;
    }
  }

  return false;
}

std::string locate_library(const std::string& name,
                           const std::vector<fs::path>& default_directories,
                           const std::vector<fs::path>& other_directories) {
  std::string path;

  locate_library("lib" + name + ".so", other_directories, path)
      || locate_library("lib" + name + ".so", default_directories, path)
      || locate_library("lib" + name + ".a", other_directories, path)
      || locate_library("lib" + name + ".a", default_directories, path);

  return path;
}

Dependencies parse_cc_dependencies(const std::string& /*executable*/,
                                   const fs::path& directory,
                                   const std::vector<std::string>& argv,
                                   const std::vector<fs::path>& default_library_directories)
{
  DependenciesResolver resolver;

  auto absolute = [&directory](const std::string& path) { return expand_path(path, directory); };

  bool openmp = false;
  std::string output_type;

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (arg == "-fopenmp") {
      openmp = true;
    } else if (is_arg(arg, ignored_single_args)) {
      continue;
    } else if (is_arg(arg, ignored_double_args)) {
      ++i;
    } else if (starts_with(arg, "-L")) {
      const std::string value = get_arg(argv, i);
      try { resolver.library_directories.emplace_back(absolute(value)); }
      catch (std::filesystem::filesystem_error&) { resolver.errors.emplace_back("Invalid -L " + value); }
    } else if (starts_with(arg, "-l")) {
      resolver.locate_and_add_library(get_arg(argv, i), default_library_directories);
    } else if (starts_with(arg, "-o")) {
      output_type = get_output_type(get_arg(argv, i));
    } else if (consume_arg(arg, "-isystem", i) || consume_arg(arg, "-I", i)) {

    } else {
      resolver.dependencies.emplace(absolute(arg).string());
    }
  }

  if (output_type == "shared") {
//    if (is_cxx(executable)) {
//      // libstdc++ requires libm
//      // https://stackoverflow.com/a/1033940
//      locate_and_add_library("stdc++", cmd);
//      locate_and_add_library("m", cmd);
//    }

//    locate_and_add_library("gcc_s", cmd);
//    locate_and_add_library("gcc", cmd);
//    locate_and_add_library("c", cmd);

    if (openmp) {
      resolver.locate_and_add_library("gomp", default_library_directories);
      resolver.locate_and_add_library("pthread", default_library_directories);
    }
  }

  return {
    {std::make_move_iterator(resolver.dependencies.begin()), std::make_move_iterator(resolver.dependencies.end())},
    std::move(resolver.errors)
  };
}

Dependencies parse_ar_dependencies(const fs::path& directory,
                                   const std::vector<std::string>& argv)
{
  DependenciesResolver resolver;

  auto absolute = [&directory](const std::string& path) { return expand_path(path, directory); };

  bool output_found = false;

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (ends_with(arg, ".a")) {
      if (!output_found)
        output_found = true;
      else
        resolver.dependencies.emplace(absolute(arg).string());
    } else if (ends_with(arg, ".o")) {
      resolver.dependencies.emplace(absolute(arg).string());
    }
  }

  return {
    {std::make_move_iterator(resolver.dependencies.begin()), std::make_move_iterator(resolver.dependencies.end())},
    std::move(resolver.errors)
  };
}

} // anonymous namespace

void DependenciesResolver::locate_and_add_library(const std::string& namespec,
                                                            const std::vector<fs::path>& default_library_directories) {
  const std::string realpath = locate_library(namespec, default_library_directories, library_directories);
  if (realpath.empty()) {
    errors.emplace_back("Unable to locate library " + namespec + "library");
  } else {
    dependencies.emplace(realpath);
  }
}


Dependencies parse_dependencies(const CompilationCommand& cmd,
                                const std::vector<fs::path>& default_library_directories)
{
  const std::vector<std::string> argv = shellwords::shellsplit(cmd.args);

  if (is_cc(cmd.executable)) {
    return parse_cc_dependencies(cmd.executable, cmd.directory, argv, default_library_directories);
  } else if (is_ar(cmd.executable)) {
    return parse_ar_dependencies(cmd.directory, argv);
  } else {
    throw std::runtime_error("Unknown executable: " + cmd.executable);
  }
}

std::string redirect_gcc_output(const CompilationCommand& command, const std::string& to) {
  const std::vector<std::string> argv = shellwords::shellsplit(command.args);

  std::ostringstream ss;
  ss << command.executable;

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (starts_with(arg, "-o")) {
      if (!to.empty())
        ss << " -o " << to;

      if (arg.size() == 2) {
        ++i;
      }
    } else {
      ss << " " << arg;
    }
  }

  return ss.str();
}

std::string redirect_ar_output(const CompilationCommand& command, const std::string& to) {
  const std::vector<std::string> argv = shellwords::shellsplit(command.args);

  std::ostringstream ss;
  ss << command.executable;

  bool output_found = false;

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (ends_with(arg, ".a") && !output_found) {
        ss << " " << to;
        output_found = true;
    }

    ss << " " << arg;
  }

  return ss.str();
}

void parse_commands(std::istream& in, const std::function<void (size_t, const std::string&, const CompilationCommand&)>& notify)
{
  INSTRMT_FUNCTION();

  std::string line;
  CompilationCommand cmd;
  size_t item = 0UL;

  while (std::getline(in, line) && !line.empty()) {
    clear(cmd);
    parse_command(line, cmd);
    notify(item, line, cmd);
    ++item;
  }
}

void parse_compile_commands(std::istream& in, const std::function<void (size_t, const std::string&, const CompilationCommand&)>& notify)
{
  INSTRMT_FUNCTION();

  pt::ptree tree;
  try { pt::read_json(in, tree); }
  catch (...) { std::throw_with_nested(std::runtime_error("Unable to parse JSON")); }

  CompilationCommand cmd;
  size_t item = 0UL;

  for(const pt::ptree::value_type& entry : tree) {
    clear(cmd);

    const auto& data = entry.second;
    cmd.directory = data.get_child("directory").data();
    const std::string line = data.get_child("command").data();
    parse_command(line, cmd, parse_command_options::expand_path);

    notify(item, line, cmd);
    ++item;
  }
}

void CommandImporter::import_commands(std::istream& in)
{
  parse_commands(in, [this](auto ...args){ on_command(args...); });
}

void CommandImporter::import_compile_commands(std::istream& in)
{
  parse_compile_commands(in, [this](auto ...args){ on_command(args...); });
}

void CommandImporter::on_command(size_t /*item*/, const std::string& /*line*/, const CompilationCommand& command)
{
  if (command.directory.empty())
    throw std::runtime_error("Invalid command: directory could not be identified");

  if (command.executable.empty())
    throw std::runtime_error("Invalid command: executable could not be identified");

  if (command.output.empty())
    throw std::runtime_error("Invalid commant: output could not be identified");

  const long long command_id = db.create_command(command.directory, command.executable, command.args);

  if (-1 == db.artifact_id_by_name(command.output)) {
    db.create_artifact(command.output, command.output_type, command_id);
  }

  ++count;
}
