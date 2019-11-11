#include "analyse-dependencies-command.hxx"

#include <iostream>
#include <algorithm>
#include <set>
#include <map>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "Database2.hxx"
#include "utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

struct CompilationOperation {
  std::string output;
  std::string output_type;
  std::map<std::string, std::string> dependencies;
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

CompilationOperation parse_cc_args(const std::vector<std::string>& args)
{
  CompilationOperation cmd;

  for(size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (is_arg(ignored_single_args, arg)) {
      continue;
    } else if (is_arg(ignored_double_args, arg)) {
      ++i;
    } else if (arg == "-isystem") {
      cmd.dependencies[args[++i]] = "system-include-dir";
    } else if (starts_with(arg, "-I")) {
      cmd.dependencies[get_arg(args, i)] = "include-dir";
    } else if (starts_with(arg, "-L")) {
      cmd.dependencies[get_arg(args, i)] = "library-dir";
    } else if (starts_with(arg, "-l")) {
      const std::string& value = get_arg(args, i);
      cmd.dependencies[value] = library_type(value);
    } else if (starts_with(arg, "-o")) {
      cmd.output = get_arg(args, i);
      cmd.output_type = output_type(cmd.output);
    } else {
      cmd.dependencies[arg] = input_type(arg);
    }
  }

  return cmd;
}

CompilationOperation parse_ar_args(const std::vector<std::string>& args)
{
  CompilationOperation cmd;
  cmd.output_type = "static";

  for(size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (ends_with(arg, ".a")) {
      if (cmd.output.empty())
        cmd.output = arg;
      else
        cmd.dependencies[arg] = "static";
    } else if (ends_with(arg, ".o")) {
      cmd.dependencies[arg] = "object";
    }
  }

  return cmd;
}

void process_command(const std::string& command, std::vector<CompilationOperation>& operations) {
  const std::vector<std::string> args = bpo::split_unix(command);
  const std::string& argv0 = args.front();

  if (is_cc(argv0)) {
    operations.emplace_back(parse_cc_args(args));
  } else if (argv0 == "ar") {
    operations.emplace_back(parse_ar_args(args));
  }
}

void process_commands(std::istream& in, std::vector<CompilationOperation>& operations) {
  std::string line;
  while (std::getline(in, line) && !line.empty()) {
    process_command(line, operations);
  }
}

void insert_artifact(std::map<std::string, std::string>& artifacts, const std::string& name, const std::string& type) {
  auto it = artifacts.find(name);
  if (it == artifacts.end()) {
    artifacts[name] = type;
  } else {
    if (it->second != type) {
      std::cerr << "Ambiguous artifact type " << name << ": " << it->second << " or " << type << std::endl;
    }
  }
}

const std::vector<std::string> dependency_types = {"source", "object", "static", "shared", "library"};

bool is_dependency_type(const std::string& type) {
  return std::find(dependency_types.begin(), dependency_types.end(), type) != dependency_types.end();
}

} // anonymous namespace

int analyse_dependencies_command(const std::vector<std::string>& command, const std::vector<std::string>& args)
{
  bpo::options_description desc("Options");

  desc.add_options()
      ("help,h", "Produce help message.")
      ("database,d", bpo::value<std::string>()->required(),
       "SQLite database to fill.")
      ("commands", bpo::value<std::vector<std::string>>()->multitoken()->default_value({"-"}, "-"),
       "Command lines to parse.\n"
       "Use - to read from cin (default).\n"
       "Use @path/to/file to read from a file.")
      ;

  bpo::positional_options_description p;
  p.add("commands", -1);

  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(desc).positional(p).run(), vm);

    if (vm.count("help")) {
      std::cout << "Usage:";
      for(const std::string& c : command)
        std::cout << " " << c;
      std::cout << " [options]" << std::endl;
      std::cout << desc;
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

  std::map<std::string, std::string> artifacts;
  for(const CompilationOperation& op : operations) {
    insert_artifact(artifacts, op.output, op.output_type);
    for (const auto& dependency: op.dependencies) {
      if (is_dependency_type(dependency.second)) {
        insert_artifact(artifacts, dependency.first, dependency.second);
      }
    }
  }

  Database2 db(vm["database"].as<std::string>());

  {
    SQLite::Transaction transaction(db.database());

    for(const auto& artifact : artifacts) {
      db.create_artifact(artifact.first, artifact.second);
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
    }

    transaction.commit();
  }

  return 0;
}
