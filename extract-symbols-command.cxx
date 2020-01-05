#include "extract-symbols-command.hxx"

#include <iostream>
#include <future>
#include <functional>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "utils.hxx"
#include "nm.hxx"
#include "Database2.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

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

boost::program_options::options_description Extract_Symbols_Command::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("artifacts",
       bpo::value<std::vector<std::string>>()->multitoken(),
       "Artifacts (.o, .so, .a ...) to process.\n"
       "Use - to read from cin.\n"
       "Use @path/to/file to read from a file.")
      ("prefix,p",
       bpo::value<std::string>()->default_value(bfs::current_path().string()),
       "If artifacts use relative paths.")
      ;

  return opt;
}

int Extract_Symbols_Command::execute(const std::vector<std::string>& args)
{
  bpo::positional_options_description p;
  p.add("artifacts", -1);

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

  auto expand = [prefix=vm["prefix"].as<std::string>()](const std::string& path) { return expand_path(path, prefix); };

  Database2 db(vm["db"].as<std::string>());
  SQLite::Transaction transaction(db.database());
  BufferedTasks<std::string> tasks(512, SymbolExtractor(db));

  if (vm.count("artifacts") > 0) {
    for(const std::string& artifact : vm["artifacts"].as<std::vector<std::string>>()) {
      if (artifact == "-") {
        std::string line;
        while (std::getline(std::cin, line)) { tasks.push(expand(line)); }
      } else if (artifact[0] == '@') {
        const bfs::path lst = expand(artifact.substr(1)); // Might be @~/...
        std::ifstream in(lst.string());
        std::string line;
        while (std::getline(in, line)) { tasks.push(expand(line)); }
      } else {
        tasks.push(expand(artifact));
      }
    }
  } else {
    SQLite::Statement q(db.database(), "select id, name, type from artifacts where type not in (\"source\", \"static\")");
    while (q.executeStep()) {
      std::string name = q.getColumn(1).getString();
      if (bfs::exists(name)) {
        tasks.push(name);
      }
    }
  }

  tasks.done();
  transaction.commit();

  std::cout << db.database().execAndGet("select count(*) from symbols").getInt64() << " symbols" << std::endl;
  std::cout << db.database().execAndGet("select count(*) from symbol_references").getInt64() << " symbol references" << std::endl;

  return 0;
}
