#include "export-dependencies-command.hxx"

#include <iostream>
#include <sstream>
#include <map>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <functional>

#include "Database2.hxx"
#include "infix_iterator.hxx"
#include "query-utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

std::map<std::string, std::string> node_color = {
  {"source", "(85,255,0,255)"},
  {"object", "(255,170,0,255)"},
  {"static", "(85,170,0,255)"},
  {"shared", "(255,85,0,255)"},
  {"library", "(85,85,0,255)"},
  {"executable", "(170,0,0,255)"}
};

class DependencyPrinter {
private:
  Database2 &db;
  std::string included_types_in_expr, excluded_types_in_expr;

  void type_conditions(const std::string& field, std::vector<std::string>& conditions) const;

  std::string artifacts_query(const char* base_query) const;

  std::string dependencies_query(const char* base_query) const;

public:
  explicit DependencyPrinter(Database2 &db) : db(db) {}

  void filter_types(const std::vector<std::string>& included, const std::vector<std::string>& excluded);

  size_t artifacts_count() const;
  void each_artifact_id(std::function<void (long long id)> cbk) const;
  void each_artifact_name(std::function<void (long long id, const std::string& name)> cbk) const;
  void each_artifact_type(std::function<void (long long id, const std::string& type)> cbk) const;

  size_t dependencies_count() const;
  void each_dependency(std::function<void (long long, long long, long long)> cbk) const;
};

void DependencyPrinter::filter_types(const std::vector<std::string>& included, const std::vector<std::string>& excluded) {
  std::stringstream ss;

  if (!included.empty()) {
    ss << "in " << in_expr(included);
    included_types_in_expr = ss.str();

    ss.clear();
  }

  if (!excluded.empty()) {
    ss << "not in " << in_expr(excluded);
    excluded_types_in_expr = ss.str();
  }
}

void DependencyPrinter::type_conditions(const std::string& field, std::vector<std::string>& conditions) const {
  if (!included_types_in_expr.empty()) conditions.emplace_back(field + " " + included_types_in_expr);
  if (!excluded_types_in_expr.empty()) conditions.emplace_back(field + " " + excluded_types_in_expr);
}

std::string DependencyPrinter::artifacts_query(const char* base_query) const {
  std::stringstream ss;
  ss << base_query;

  std::vector<std::string> conditions; type_conditions("artifacts.type", conditions);

  if (!conditions.empty()) {
    ss << "\nwhere ";
    std::copy(conditions.cbegin(), conditions.cend(), infix_ostream_iterator<std::string>(ss, "\nand "));
  }

  return ss.str();
}

std::string DependencyPrinter::dependencies_query(const char* base_query) const {
  std::stringstream ss;
  ss << base_query;

  std::vector<std::string> conditions; type_conditions("la.type", conditions); type_conditions("ra.type", conditions);

  if (!conditions.empty()) {
    ss << "\ninner join artifacts as la on la.id = dependencies.dependee_id";
    ss << "\ninner join artifacts as ra on ra.id = dependencies.dependency_id";
    ss << "\nwhere ";
    std::copy(conditions.cbegin(), conditions.cend(), infix_ostream_iterator<std::string>(ss, "\nand "));
  }

  return ss.str();
}

size_t DependencyPrinter::artifacts_count() const {
  return db.database().execAndGet(artifacts_query("select count(*) from artifacts")).getInt64();
}

void DependencyPrinter::each_artifact_id(std::function<void (const long long)> cbk) const {
  SQLite::Statement q(db.database(), artifacts_query("select id from artifacts"));
  while (q.executeStep()) { cbk(q.getColumn(0).getInt64()); }
}

void DependencyPrinter::each_artifact_name(std::function<void (const long long, const std::string&)> cbk) const {
  SQLite::Statement q(db.database(), artifacts_query("select id, name from artifacts"));
  while (q.executeStep()) { cbk(q.getColumn(0).getInt64(), q.getColumn(1).getString()); }
}

void DependencyPrinter::each_artifact_type(std::function<void (const long long, const std::string&)> cbk) const {
  SQLite::Statement q(db.database(), artifacts_query("select id, type from artifacts"));
  while (q.executeStep()) { cbk(q.getColumn(0).getInt64(), q.getColumn(1).getString()); }
}

size_t DependencyPrinter::dependencies_count() const {
  return db.database().execAndGet(dependencies_query("select count(*) from dependencies")).getInt64();
}

void DependencyPrinter::each_dependency(std::function<void (long long, long long, long long)> cbk) const {
  SQLite::Statement q(db.database(), dependencies_query("select dependencies.id, dependencies.dependee_id, dependencies.dependency_id from dependencies"));
  while (q.executeStep()) { cbk(q.getColumn(0).getInt64(), q.getColumn(1).getInt64(), q.getColumn(2).getInt64()); }
}

class TLPDependencyPrinter : public DependencyPrinter {
public:
  using DependencyPrinter::DependencyPrinter;

  void print(std::ostream& out);
};

void TLPDependencyPrinter::print(std::ostream &out) {
  out << "(tlp \"2.3\"\n";

  const int nb_nodes = artifacts_count();
  std::cout << "(nb_nodes " << nb_nodes << ")\n";
  std::cout << "(nodes 0.." << (nb_nodes - 1) << ")\n";

  std::map<long long, long long> nodes;
  {
    long long i = 0;
    each_artifact_id([&i, &nodes](const long long id){ nodes[id] = i++; });
  }

  const int nb_edges = dependencies_count();
  std::cout << "(nb_edges " << nb_edges << ")\n";

  {
    long long i = 0;
    each_dependency([&i, &out, &nodes](long long, long long dependee_id, long long dependency_id){
      out << "(edge " << i++ << " " << nodes[dependee_id] << " " << nodes[dependency_id] << ")\n";
    });
  }

  out << "(property 0 string \"viewLabel\"\n"
         "(default \"\" \"\")\n";
  each_artifact_name([&out, &nodes](const long long id, const bfs::path& path){
    out << "(node " << nodes[id] << " \"" << path.filename().string() << "\")\n";
  });
  out << ")\n";

  out << "(property 0 color \"viewColor\"\n"
         "(default \"(255,95,95,255)\" \"(180,180,180,255)\")\n";
  each_artifact_type([&out, &nodes](const long long id, const std::string& type){
    out << "(node " << nodes[id] << " \"" << node_color[type] << "\")\n";
  });
  out << ")\n";

  out << ")" << std::endl;
}

} // anonymous namespace

int export_dependencies_command(const std::vector<std::string>& command, const std::vector<std::string>& args)
{
  bpo::options_description desc("Options");

  desc.add_options()
      ("help,h", "Produce help message.")
      ("database,d", bpo::value<std::string>()->required(),
       "SQLite database.")
      ("type", bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type", bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ;

  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(desc).run(), vm);

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

  TLPDependencyPrinter printer(db);
  printer.filter_types(vm["type"].as<std::vector<std::string>>(), vm["not-type"].as<std::vector<std::string>>());
  printer.print(std::cout);

  return 0;
}
