#include "extract-command.hxx"

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
#include "nm.hxx"

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

CompilationOperation parse_cc_args(const bfs::path& wd, const std::vector<std::string>& argv)
{
  CompilationOperation cmd;

  auto absolute = [&wd](const std::string& path) { return expand_path(path, wd); };

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

    if (is_arg(ignored_single_args, arg)) {
      continue;
    } else if (is_arg(ignored_double_args, arg)) {
      ++i;
    } else if (arg == "-isystem") {
      cmd.add_system_include_dir(absolute(argv[++i]));
    } else if (starts_with(arg, "-I")) {
      const std::string value = get_arg(argv, i);
      try { cmd.add_include_dir(absolute(value)); }
      catch (boost::filesystem::filesystem_error&) { cmd.add_error("Invalid -I: " + value); }
    } else if (starts_with(arg, "-L")) {
      const std::string value = get_arg(argv, i);
      try { cmd.add_library_dir(absolute(value)); }
      catch (boost::filesystem::filesystem_error&) { cmd.add_error("Invalid -L: " + value); }
    } else if (starts_with(arg, "-l")) {
      const std::string value = get_arg(argv, i);
      const std::string realpath = locate_library(value, default_library_directories(), cmd.library_directories);
      if (realpath.empty()) { cmd.add_error("Invalid -l: " + value); }
      else { cmd.add_dependency(realpath, library_type(realpath)); }
    } else if (starts_with(arg, "-o")) {
      cmd.output = absolute(get_arg(argv, i));
      cmd.output_type = output_type(cmd.output);
    } else {
      cmd.add_dependency(absolute(arg), input_type(arg));
    }
  }

  return cmd;
}

CompilationOperation parse_ar_args(const bfs::path& wd, const std::vector<std::string>& argv)
{
  CompilationOperation cmd;
  cmd.output_type = "static";

  auto absolute = [&wd](const std::string& path) { return expand_path(path, wd); };

  for(size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];

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

CompilationOperation parse_command(const bfs::path& directory, const std::string& executable, const std::string& args) {
  const std::vector<std::string> argv = bpo::split_unix(args);

  if (is_cc(executable)) {
    return parse_cc_args(directory, argv);
  } else if (executable == "ar") {
    return parse_ar_args(directory, argv);
  } else {
    throw std::runtime_error("Unknown executable: " + executable);
  }
}

//const std::vector<std::string> dependency_types = {"source", "object", "static", "shared", "library"};

//bool is_dependency_type(const std::string& type) {
//  return std::find(dependency_types.begin(), dependency_types.end(), type) != dependency_types.end();
//}

//std::vector<CompilationOperation> parse_commands(Database2& db) {
//  std::vector<CompilationOperation> operations;

//  auto stm = db.statement("select id, directory, executable, args from commands");
//  while (stm.executeStep()) {
//    const long long id = stm.getColumn(0).getInt64();
//    const bfs::path directory = stm.getColumn(1).getString();
//    const std::string executable = stm.getColumn(2).getString();
//    const std::string args= stm.getColumn(3).getString();

//    operations.emplace_back(parse_command(directory, executable, args));
//  }

//  return operations;
//}

long long upsert_artifact(Database2& db, const std::string& name, const std::string& type, const long long generating_command_id = -1)
{
  Artifact artifact; artifact.name = name;

  if (!db.artifact_by_name(artifact)) {
    db.create_artifact(name, type, generating_command_id);
    artifact.id = db.last_id();
  } else {
    if (artifact.type != type) {
      throw std::runtime_error("Conflicting type");
    }

    if (artifact.generating_command_id == -1) {
      if (generating_command_id != -1) {
        db.artifact_set_generating_command(artifact.id, generating_command_id);
      }
    } else {
      if (artifact.generating_command_id != generating_command_id && generating_command_id != -1) {
        throw std::runtime_error("Conflicting generating_command_id");
      }
    }
  }

  return artifact.id;
}

void extract_dependencies(Database2& db) {
  auto stm = db.statement("select id, directory, executable, args from commands");
  while (stm.executeStep()) {
    const long long command_id = stm.getColumn(0).getInt64();
    const bfs::path directory = stm.getColumn(1).getString();
    const std::string executable = stm.getColumn(2).getString();
    const std::string args= stm.getColumn(3).getString();

    const CompilationOperation op = parse_command(directory, executable, args);

    const long long artifact_id = upsert_artifact(db, op.output, op.output_type, command_id);

    std::cout << artifact_id << " " << op.output << ": " << op.output_type << std::endl;

    for(const std::string& err : op.errors) {
      std::cout << err << std::endl;
    }

    for (const auto& dependency: op.dependencies) {
      const long long dependency_id = upsert_artifact(db, dependency.first, dependency.second);
      std::cout << "\t" << dependency_id << " " << dependency.first << ": " << dependency.second << std::endl;
      db.create_dependency(artifact_id, dependency_id);
    }
  }
}

void extract_symbols_from_file(const std::string& usable_path, ArtifactSymbols& symbols) {
  nm_undefined(usable_path, symbols.undefined);
  nm_defined_extern(usable_path, symbols.external);
  nm_defined(usable_path, symbols.internal);

//  std::future<void> f2 = std::async(std::launch::async, [&usable_path, &external_set=symbols.external]{ nm_defined_extern(usable_path, external_set); });
//  std::future<void> f3 = std::async(std::launch::async, [&usable_path, &internal_set=symbols.internal]{ nm_defined(usable_path, internal_set); });
//  std::future<void> f1 = std::async(std::launch::async, [&usable_path, &undefined_set=symbols.undefined]{ nm_undefined(usable_path, undefined_set); });

//  f2.wait(); f3.wait();

  substract_set(symbols.internal, symbols.external);

//  f1.wait();
}

void insert_symbols(Database2& db, const std::string& path, ArtifactSymbols& symbols) {
  long long artifact_id = db.artifact_id_by_name(path);
  if (artifact_id == -1) {
    db.create_artifact(path, output_type(path), false);
    artifact_id = db.last_id();
  }

  std::cout << artifact_id << " " << path << std::endl;

  db.insert_symbol_references(artifact_id, symbols);
}

class SymbolExtractor {
private:
  Database2& db;
  std::vector<ArtifactSymbols> symbols;

public:
  explicit SymbolExtractor(Database2& db) : db(db), symbols() {}

  void operator()(const std::vector<std::string>& files) {
    while(symbols.size() < files.size()) symbols.emplace_back();

#pragma omp parallel for
    for(size_t i = 0; i < files.size(); ++i) {
      symbols[i].undefined.clear();
      symbols[i].external.clear();
      symbols[i].internal.clear();

      extract_symbols_from_file(files[i], symbols[i]);
    }

    for(size_t i = 0; i < files.size(); ++i) {
      insert_symbols(db, files[i], symbols[i]);
    }
  }
};

template<typename T>
class BufferedTasks {
private:
  std::vector<T> buffer;
  size_t capacity;
  std::function<void(const std::vector<T>&)> process;

public:
  BufferedTasks(size_t N, std::function<void(const std::vector<T>&)> processor) : buffer(), capacity(N), process(processor) {
    buffer.reserve(N);
  }

  void processBuffer() {
    process(buffer);
    buffer.clear();
  }

  void push(const T& t) {
    buffer.push_back(t);
    if (buffer.size() == capacity)
      processBuffer();
  }

  void done() { processBuffer(); }
};

} // anonymous namespace

boost::program_options::options_description Extract_Command::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("dependencies", "Extract dependencies from commands.")
      ("symbols", "Extract symbols from artifacts.")
      ;

  return opt;
}

int Extract_Command::execute(const std::vector<std::string>& args)
{
  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);

    if (vm.count("help")) {
      usage(std::cout);
      return 0;
    }

    bpo::notify(vm);
  } catch(bpo::error &err) {
    std::cerr << err.what() << std::endl;
    return -1;
  }

  Database2 db(vm["db"].as<std::string>());

  if (vm.count("dependencies")) {
    SQLite::Transaction transaction(db.database());
    extract_dependencies(db);
    transaction.commit();
  }

  if (vm.count("symbols")) {
    SQLite::Transaction transaction(db.database());

    BufferedTasks<std::string> tasks(512, SymbolExtractor(db));

    SQLite::Statement q(db.database(), "select id, name, type from artifacts where type not in (\"source\", \"static\")");
    while (q.executeStep()) {
      std::string name = q.getColumn(1).getString();
      if (bfs::exists(name)) {
        tasks.push(name);
      }
    }

    tasks.done();

    transaction.commit();
  }

  db.optimize();

  return 0;
}
