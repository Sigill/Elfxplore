#include "extract-dependencies-command.hxx"

#include <iostream>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <regex>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include "Database2.hxx"
#include "utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
namespace bp = boost::process;

namespace {

std::vector<bfs::path> load_default_library_directories() {
  std::regex search_dir_regex(R"CMD(SEARCH_DIR\("=?([^"]+)"\))CMD");
  bp::ipstream pipe_stream;
  bp::child c("gcc -Xlinker --verbose", bp::std_out > pipe_stream, bp::std_err > bp::null);

  std::vector<bfs::path> directories;

  std::string line;
  while (pipe_stream && std::getline(pipe_stream, line)) {
    auto matches_begin = std::sregex_iterator(line.begin(), line.end(), search_dir_regex);
    auto matches_end = std::sregex_iterator();
    for (std::sregex_iterator it = matches_begin; it != matches_end; ++it) {
      std::smatch match = *it;
      directories.emplace_back(match[1].str());
    }
  }

  c.wait();

  return directories;
}

const std::vector<bfs::path>& default_library_directories() {
  static const std::vector<bfs::path> directories = load_default_library_directories();
  return directories;
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

struct CompilationOperation {
  std::string output;
  std::string output_type;
  std::map<std::string, std::string> dependencies;

  std::vector<std::string> system_include_directories;
  std::vector<std::string> include_directories;
  std::vector<bfs::path> library_directories;

  std::vector<std::string> errors;

  void add_system_include_dir(std::string path) {
    system_include_directories.emplace_back(std::move(path));
  }

  void add_include_dir(std::string path) {
    include_directories.emplace_back(std::move(path));
  }

  void add_library_dir(std::string path) {
    library_directories.emplace_back(std::move(path));
  }

  void add_dependency(const std::string& path, std::string type) {
    dependencies[path] = std::move(type);
  }

  void add_error(std::string err) {
    errors.emplace_back(std::move(err));
  }
};

const std::vector<std::string> ignored_single_args = {"-D", "-w", "-W", "-O", "-m", "-g", "-f", "-MD", "-c",
                                                      "-std", "-rdynamic", "-shared", "-fopenmp", "-pipe",
                                                      "-ansi", "-pedantic"};
const std::vector<std::string> ignored_double_args = {"-MT", "-MF"};

bool is_arg(const std::vector<std::string>& prefixes, const std::string& arg) {
  return std::any_of(prefixes.begin(), prefixes.end(), [&arg](const std::string& prefix){ return starts_with(arg, prefix); });
}

std::string get_arg(const std::vector<std::string>& args, size_t& i) {
  if (args[i].size() == 2) {
    return args[++i];
  } else {
    return args[i].substr(2);
  }
}

const std::vector<std::string> gcc_commands = {"cc", "c++", "gcc", "g++"};

bool is_cc(const std::string& command) {
  return std::find(gcc_commands.begin(), gcc_commands.end(), command) != gcc_commands.end();
}

CompilationOperation parse_cc_args(const std::vector<std::string>& args, const bfs::path& wd)
{
  CompilationOperation cmd;

  auto absolute = [&wd](const std::string& path) { return expand_path(path, wd); };

  for(size_t i = 2; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (is_arg(ignored_single_args, arg)) {
      continue;
    } else if (is_arg(ignored_double_args, arg)) {
      ++i;
    } else if (arg == "-isystem") {
      cmd.add_system_include_dir(absolute(args[++i]));
    } else if (starts_with(arg, "-I")) {
      const std::string argv = get_arg(args, i);
      try { cmd.add_include_dir(absolute(argv)); }
      catch (boost::filesystem::filesystem_error&) { cmd.add_error("Invalid -I: " + argv); }
    } else if (starts_with(arg, "-L")) {
      const std::string argv = get_arg(args, i);
      try { cmd.add_library_dir(absolute(argv)); }
      catch (boost::filesystem::filesystem_error&) { cmd.add_error("Invalid -L: " + argv); }
    } else if (starts_with(arg, "-l")) {
      const std::string argv = get_arg(args, i);
      const std::string realpath = locate_library(argv, default_library_directories(), cmd.library_directories);
      if (realpath.empty()) { cmd.add_error("Invalid -l: " + argv); }
      else { cmd.add_dependency(realpath, library_type(realpath)); }
    } else if (starts_with(arg, "-o")) {
      cmd.output = absolute(get_arg(args, i));
      cmd.output_type = output_type(cmd.output);
    } else {
      cmd.add_dependency(absolute(arg), input_type(arg));
    }
  }

  return cmd;
}

CompilationOperation parse_ar_args(const std::vector<std::string>& args, const bfs::path& wd)
{
  CompilationOperation cmd;
  cmd.output_type = "static";

  auto absolute = [&wd](const std::string& path) { return expand_path(path, wd); };

  for(size_t i = 2; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (ends_with(arg, ".a")) {
      if (cmd.output.empty())
        cmd.output = absolute(arg);
      else
        cmd.add_dependency(absolute(arg), "static");
    } else if (ends_with(arg, ".o")) {
      cmd.add_dependency(absolute(arg), "object");
    }
  }

  return cmd;
}

void process_command(const std::string& command, std::vector<CompilationOperation>& operations) {
  const std::vector<std::string> args = bpo::split_unix(command);
  const std::string& wd = args[0];
  const std::string& argv0 = args[1];

  if (is_cc(argv0)) {
    operations.emplace_back(parse_cc_args(args, wd));
  } else if (argv0 == "ar") {
    operations.emplace_back(parse_ar_args(args, wd));
  }
}

void process_commands(std::istream& in, std::vector<CompilationOperation>& operations) {
  std::string line;
  while (std::getline(in, line) && !line.empty()) {
    process_command(line, operations);
  }
}

struct ArtifactAttribute {
  std::string type;
  bool generated;

  explicit ArtifactAttribute(std::string type)
    : type(std::move(type))
    , generated(false)
  {}
};

void insert_artifact(std::map<std::string, ArtifactAttribute>& artifacts, const std::string& name, const std::string& type) {
  auto it = artifacts.find(name);
  if (it == artifacts.end()) {
    artifacts.emplace(name, type);
  } else {
    if (it->second.type != type) {
      std::cerr << "Ambiguous artifact type " << name << ": " << it->second.type << " or " << type << std::endl;
    }
  }
}

const std::vector<std::string> dependency_types = {"source", "object", "static", "shared", "library"};

bool is_dependency_type(const std::string& type) {
  return std::find(dependency_types.begin(), dependency_types.end(), type) != dependency_types.end();
}

} // anonymous namespace

boost::program_options::options_description Extract_Dependencies_Command::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("commands", bpo::value<std::vector<std::string>>()->multitoken()->default_value({"-"}, "-"),
       "Command lines to parse.\n"
       "Use - to read from cin (default).\n"
       "Use @path/to/file to read from a file.")
      ;

  return opt;
}

int Extract_Dependencies_Command::execute(const std::vector<std::string>& args)
{
  bpo::positional_options_description p;
  p.add("commands", -1);

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

  const std::vector<std::string> command_lines = vm["commands"].as<std::vector<std::string>>();
  std::vector<CompilationOperation> operations;

  for(const std::string& command : command_lines) {
    if (command == "-") {
      process_commands(std::cin, operations);
    } else if (command[0] == '@') {
      const bfs::path lst = expand_path(command.substr(1)); // Might be @~/...
      std::ifstream in(lst.string());
      process_commands(in, operations);
    } else {
      process_command(command, operations);
    }
  }

  std::map<std::string, ArtifactAttribute> artifacts;
  for(const CompilationOperation& op : operations) {
    insert_artifact(artifacts, op.output, op.output_type);
    artifacts.at(op.output).generated = true;
    for (const auto& dependency: op.dependencies) {
      if (is_dependency_type(dependency.second)) {
        insert_artifact(artifacts, dependency.first, dependency.second);
      }
    }
  }

  Database2 db(vm["db"].as<std::string>());

  {
    SQLite::Transaction transaction(db.database());

    for(const auto& artifact : artifacts) {
      db.create_artifact(artifact.first, artifact.second.type, artifact.second.generated);
    }

    transaction.commit();
  }

  std::cout << "Artifacts created" << std::endl;

  {
    SQLite::Transaction transaction(db.database());

    for(const CompilationOperation& op : operations) {
      int dependee_id = db.artifact_id_by_name(op.output);
      std::cout << dependee_id << " " << op.output << ": " << op.output_type << std::endl;

      for (const auto& dependency: op.dependencies) {
        if (is_dependency_type(dependency.second)) {
          std::cout << "\t" << dependency.first << ": " << dependency.second << std::endl;
          db.create_dependency(dependee_id, db.artifact_id_by_name(dependency.first));
        }
      }

      for (const auto& error: op.errors) {
        std::cout << "\tERROR " << error << std::endl;
      }
    }

    transaction.commit();
  }

  return 0;
}
