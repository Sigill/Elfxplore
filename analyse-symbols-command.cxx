#include "analyse-symbols-command.hxx"

#include <iostream>
#include <future>
#include <regex>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "utils.hxx"
#include "nm.hxx"
#include "Database2.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

void process_artifact(Database2& db, const std::string& logical_path, const bfs::path& prefix) {
  const bfs::path usable_path = prefix.empty() ? logical_path : (prefix / logical_path);

  long long artifact_id = db.artifact_id_by_name(logical_path);
  if (artifact_id == -1) {
    db.create_artifact(logical_path, output_type(logical_path));
    artifact_id = db.last_id();
  }

  std::cout << artifact_id << " " << logical_path << std::endl;

  ArtifactSymbols symbols;

//    std::future<SymbolSet> f1 = std::async(std::launch::async, [&usable_path]{ return nm_undefined(usable_path.string()); });
//    std::future<SymbolSet> f2 = std::async(std::launch::async, [&usable_path]{ return nm_defined_extern(usable_path.string()); });
//    std::future<SymbolSet> f3 = std::async(std::launch::async, [&usable_path]{ return nm_defined(usable_path.string()); });

//    f1.wait(), f2.wait(), f3.wait();

//    symbols.undefined = f1.get();
//    symbols.external = f2.get();
//    symbols.internal = f3.get();

  symbols.undefined = nm_undefined(usable_path.string());
  symbols.external = nm_defined_extern(usable_path.string());
  symbols.internal = nm_defined(usable_path.string());

  substract_set(symbols.internal, symbols.external);

  db.insert_symbol_references(artifact_id, symbols);
}

void process_artifacts(Database2& db, std::istream& in, const bfs::path& prefix) {
  std::string line;
  while (std::getline(in, line) && !line.empty()) {
    process_artifact(db, line, prefix);
  }
}

} // anonymous namespace

int analyse_symbols_command(const std::vector<std::string>& command, const std::vector<std::string>& args)
{
  bpo::options_description desc("Options");

  desc.add_options()
      ("help,h", "Produce help message.")
      ("database,d", bpo::value<std::string>()->required(),
       "SQLite database to fill.")
      ("artifacts", bpo::value<std::vector<std::string>>()->multitoken()->default_value({"-"}, "-"),
       "Artifacts (.o, .so, .a ...) to process.\n"
       "Use - to read from cin (default).\n"
       "Use @path/to/file to read from a file.")
      ("prefix,p", bpo::value<std::string>()->default_value(""),
       "If artifacts use relative paths.")
      ;

  bpo::positional_options_description p;
  p.add("artifacts", -1);

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

  Database2 db(vm["database"].as<std::string>());

  const bfs::path prefix = vm["prefix"].as<std::string>();

  SQLite::Transaction transaction(db.database());

  for(const std::string& artifact : vm["artifacts"].as<std::vector<std::string>>()) {
    if (artifact == "-") {
      process_artifacts(db, std::cin, prefix);
    } else if (artifact[0] == '@') {
      const bfs::path lst = expand_path(artifact.substr(1)); // Might be @~/...
      std::ifstream in(lst.string());
      process_artifacts(db, in, prefix);
    } else {
      process_artifact(db, artifact, prefix);
    }
  }

  transaction.commit();

  std::cout << db.database().execAndGet("select count(*) from symbols").getInt64() << " symbols" << std::endl;
  std::cout << db.database().execAndGet("select count(*) from symbol_references").getInt64() << " symbol references" << std::endl;

  return 0;
}
