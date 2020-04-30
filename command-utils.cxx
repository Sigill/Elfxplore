#include "command-utils.hxx"

#include <boost/tokenizer.hpp>
#include <boost/program_options.hpp>
#include <boost/process.hpp>

#include "utils.hxx"

namespace bfs = boost::filesystem;
namespace bpo = boost::program_options;
namespace bp = boost::process;

using Tokenizer = boost::tokenizer<boost::escaped_list_separator<char>, typename std::string::const_iterator, std::string>;

namespace {

void consume_token(Tokenizer::iterator& it, const Tokenizer::iterator& end, std::string& out) {
  if (it != end) {
    out = *it;
    ++it;
  }
}

void parse_cc_command(Tokenizer::iterator& it, const Tokenizer::iterator& end, CompilationCommand& command) {
  for (; it != end; ++it) {
    if (starts_with(*it, "-o")) {
      if (it->size() > 2) {
        command.output = it->substr(2);
      } else {
        if (++it != end)
          command.output = *it;
      }
    }
  }
}

void parse_ar_command(Tokenizer::iterator& it, const Tokenizer::iterator& end, CompilationCommand& command) {
  for (; it != end; ++it) {
    if (ends_with(*it, ".a") && command.output.empty())
      command.output = *it;
  }
}

const std::vector<std::string> gcc_commands = {"cc", "c++", "gcc", "g++"};

} // anonymous namespace

bool is_cc(const std::string& command) {
  return std::find(gcc_commands.begin(), gcc_commands.end(), command) != gcc_commands.end();
}

void parse_command(const std::string& line, CompilationCommand& command)
{
  Tokenizer tok(line.begin(), line.end(), boost::escaped_list_separator<char>("\\", " \t", "'\""));

  typename Tokenizer::iterator token_it(tok.begin()), token_end(tok.end());

  consume_token(token_it, token_end, command.directory);
  consume_token(token_it, token_end, command.executable);

  if (token_it != token_end) {
    command.args = std::string(token_it.base(), line.end());

    if (is_cc(command.executable)) {
      parse_cc_command(token_it, token_end, command);
    } else if (command.executable == "ar") {
      parse_ar_command(token_it, token_end, command);
    }

    if (!command.output.empty())
      command.output_type = get_output_type(command.output);
  }
}

bool CompilationCommand::is_complete() const
{
  return !directory.empty() || !executable.empty() || !args.empty() || !output.empty() || !output_type.empty();
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

bool locate_library(const std::string& name, const std::vector<bfs::path>& directories, std::string& out) {
  for(const bfs::path& dir : directories) {
    const bfs::path candidate = dir / name;
    if (bfs::exists(candidate)) {
      out = bfs::canonical(candidate).string();
      return true;
    }
  }

  return false;
}

std::string locate_library(const std::string& name,
                           const std::vector<bfs::path>& default_directories,
                           const std::vector<bfs::path>& other_directories) {
  std::string path;

  locate_library("lib" + name + ".so", other_directories, path)
      || locate_library("lib" + name + ".so", default_directories, path)
      || locate_library("lib" + name + ".a", other_directories, path)
      || locate_library("lib" + name + ".a", default_directories, path);

  return path;
}

void parse_cc_dependencies(const std::string& /*executable*/,
                   const bfs::path& directory,
                   const std::vector<std::string>& argv,
                   const std::vector<bfs::path>& default_library_directories,
                   CompilationCommandDependencies& cmd)
{
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
      try { cmd.library_directories.emplace_back(absolute(value)); }
      catch (boost::filesystem::filesystem_error&) { cmd.errors.emplace_back("Invalid -L " + value); }
    } else if (starts_with(arg, "-l")) {
      cmd.locate_and_add_library(get_arg(argv, i), default_library_directories);
    } else if (starts_with(arg, "-o")) {
      output_type = get_output_type(get_arg(argv, i));
    } else if (consume_arg(arg, "-isystem", i) || consume_arg(arg, "-I", i)) {

    } else {
      cmd.dependencies.emplace(absolute(arg));
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
      cmd.locate_and_add_library("gomp", default_library_directories);
      cmd.locate_and_add_library("pthread", default_library_directories);
    }
  }
}

void parse_ar_dependencies(const bfs::path& directory,
                   const std::vector<std::string>& argv,
                   CompilationCommandDependencies& cmd)
{
  auto absolute = [&directory](const std::string& path) { return expand_path(path, directory); };

  bool output_found = false;

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (ends_with(arg, ".a")) {
      if (!output_found)
        output_found = true;
      else
        cmd.dependencies.emplace(absolute(arg));
    } else if (ends_with(arg, ".o")) {
      cmd.dependencies.emplace(absolute(arg));
    }
  }
}

} // anonymous namespace

void CompilationCommandDependencies::locate_and_add_library(const std::string& namespec,
                                                            const std::vector<bfs::path>& default_library_directories) {
  const std::string realpath = locate_library(namespec, default_library_directories, library_directories);
  if (realpath.empty()) {
    errors.emplace_back("Unable to locate library " + namespec + "library");
  } else {
    dependencies.emplace(realpath);
  }
}

CompilationCommandDependencies parse_dependencies(const boost::filesystem::path& directory,
                                                  const std::string& executable,
                                                  const std::string& args,
                                                  const std::vector<bfs::path>& default_library_directories)
{
  const std::vector<std::string> argv = bpo::split_unix(args);

  CompilationCommandDependencies dependencies;

  if (is_cc(executable)) {
    parse_cc_dependencies(executable, directory, argv, default_library_directories, dependencies);
  } else if (executable == "ar") {
    parse_ar_dependencies(directory, argv, dependencies);
  } else {
    throw std::runtime_error("Unknown executable: " + executable);
  }

  return dependencies;
}
