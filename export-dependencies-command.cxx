#include "export-dependencies-command.hxx"

#include <iostream>
#include <sstream>
#include <map>
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <functional>

#include "Database2.hxx"
#include "infix_iterator.hxx"
#include "query-utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

struct tlp_fmt {
  const std::array<unsigned char, 3>& c;

  explicit tlp_fmt(const std::array<unsigned char, 3>& c) : c(c) {}

  std::ostream& operator()(std::ostream& out) const {
    return out << "(" << int(c[0]) << "," << int(c[1]) << "," << int(c[2]) << ",255)";
  }
};

std::ostream& operator<<(std::ostream& out, const tlp_fmt& c) {
  return c(out);
}

struct hex_fmt {
  const std::array<unsigned char, 3>& c;

  explicit hex_fmt(const std::array<unsigned char, 3>& c) : c(c) {}

  std::ostream& operator()(std::ostream& out) const {
    auto flags = out.flags();
    const char fill = out.fill('0');
    out << "#" << std::noshowbase << std::hex;
    out << std::setw(2) << int(c[0])
        << std::setw(2) << int(c[1])
        << std::setw(2) << int(c[2]);
    out.flags(flags);
    out.fill(fill);
    return out;
  }
};

std::ostream& operator<<(std::ostream& out, const hex_fmt& color) {
  return color(out);
}

const std::map<std::string, std::array<unsigned char, 3>> node_color = {
  {"source", {85,255,0}},
  {"object", {255,170,0}},
  {"static", {85,170,0}},
  {"shared", {255,5,0}},
  {"library", {85,85,0}},
  {"executable", {170,0,0}}
};

std::set<Dependency> get_all_dependencies(Database2& db,
                                          const std::vector<std::string>& included,
                                          const std::vector<std::string>& excluded)
{
  std::stringstream ss;
  ss << "select dependee_id, dependency_id from dependencies";
  if (!included.empty() || !excluded.empty())
    ss << " where";

  if (!included.empty()) {
    ss << " artifacts.type in " << in_expr(included);
  }

  if (!excluded.empty()) {
    if (!included.empty())
      ss << " and";

    ss << " artifacts.type not in " << in_expr(excluded);
  }

  std::set<Dependency> dependencies;
  auto stm = db.statement(ss.str());
  while (stm.executeStep()) {
    dependencies.emplace(stm.getColumn(0).getInt64(), stm.getColumn(1).getInt64());
  }

  return dependencies;
}

SQLite::Statement build_get_depend_stm(Database2& db,
                                       const std::string& select_field,
                                       const std::string& match_field,
                                       const std::vector<std::string>& included,
                                       const std::vector<std::string>& excluded)
{
  std::stringstream ss;
  ss << "select " << select_field << " from dependencies";

  if (!included.empty() || !excluded.empty())
    ss << " inner join artifacts on artifacts.id = dependencies." << select_field;

  ss << " where " << match_field << " = ?";

  if (!included.empty())
    ss << " and artifacts.type in " << in_expr(included);

  if (!excluded.empty())
    ss << " and artifacts.type not in " << in_expr(excluded);

  return db.statement(ss.str());
}

std::set<Dependency> get_all_dependencies_for(Database2& db,
                                              long long artifact_id,
                                              const std::vector<std::string>& included,
                                              const std::vector<std::string>& excluded)
{
  SQLite::Statement dependencies_stm = build_get_depend_stm(db, "dependency_id", "dependee_id", included, excluded);

  std::set<Dependency> dependencies;
  std::set<long long> visited, queue = {artifact_id};

  while(!queue.empty()) {
    auto last = std::prev(queue.end());
    long long current_dependee_id = *last;
    queue.erase(last);

    if (visited.find(current_dependee_id) == visited.end()) {
      dependencies_stm.bind(1, current_dependee_id);

      for(long long dependency_id : Database2::get_ids(dependencies_stm)) {
        dependencies.emplace(current_dependee_id, dependency_id);

        queue.insert(dependency_id);
      }
    }

    visited.insert(current_dependee_id);
  }

  return dependencies;
}

std::set<Dependency> get_all_dependees_for(Database2& db,
                                           long long artifact_id,
                                           const std::vector<std::string>& included,
                                           const std::vector<std::string>& excluded)
{
  SQLite::Statement dependees_stm = build_get_depend_stm(db, "dependee_id", "dependency_id", included, excluded);

  std::set<Dependency> dependencies;
  std::set<long long> visited, queue = {artifact_id};

  while(!queue.empty()) {
    auto last = std::prev(queue.end());
    long long current_dependency_id = *last;
    queue.erase(last);

    if (visited.find(current_dependency_id) == visited.end()) {
      dependees_stm.bind(1, current_dependency_id);

      for(long long dependee_id : Database2::get_ids(dependees_stm)) {
        dependencies.emplace(current_dependency_id, dependee_id);

        queue.insert(dependee_id);
      }
    }

    visited.insert(current_dependency_id);
  }

  return dependencies;
}

struct Artifact {
  size_t id;
  std::string name;
  std::array<unsigned char, 3> color;

  Artifact(size_t id, std::string name, std::array<unsigned char, 3> color)
    : id(id), name(std::move(name)), color(color)
  {}
};

std::set<long long> list_artifacts(const std::set<Dependency>& dependencies)
{
  std::set<long long> ids;
  for(const Dependency& d : dependencies) {
    ids.insert(d.dependee_id);
    ids.insert(d.dependency_id);
  }
  return ids;
}

std::string build_artifacts_query(const std::set<long long>& artifacts)
{
  std::stringstream ss;
  ss << "select id, name, type from artifacts where id in " << in_expr(std::vector<long long>(artifacts.begin(), artifacts.end()));
  return ss.str();
}

std::map<long long, Artifact> map_artifacts(Database2& db, const std::set<Dependency>& dependencies)
{
  std::map<long long, Artifact> mapping;

  SQLite::Statement q = db.statement(build_artifacts_query(list_artifacts(dependencies)));

  size_t i = 0;
  while (q.executeStep()) {
    mapping.emplace(std::piecewise_construct,
                    std::forward_as_tuple(q.getColumn(0).getInt64()),
                    std::forward_as_tuple(i++,
                                          bfs::path(q.getColumn(1).getString()).filename().string(),
                                          node_color.at(q.getColumn(2).getString())
                                          )
                    );
  }

  return mapping;
}

struct tlp_file_format {
  Database2& db;
  const std::set<Dependency>& dependencies;

  tlp_file_format(Database2& db, const std::set<Dependency>& dependencies)
    : db(db), dependencies(dependencies) {}

  std::ostream& operator()(std::ostream& out) const {
    const std::map<long long, Artifact> mapping = map_artifacts(db, dependencies);

    out << "(tlp \"2.3\"\n";

    std::cout << "(nb_nodes " << mapping.size() << ")\n";
    std::cout << "(nodes 0.." << (mapping.size() - 1) << ")\n";

    std::cout << "(nb_edges " << dependencies.size() << ")\n";

    std::for_each(dependencies.cbegin(), dependencies.cend(), [i=0ul, &out, &mapping] (const Dependency& dependency) mutable {
      out << "(edge " << i++ << " " << mapping.at(dependency.dependee_id).id << " " << mapping.at(dependency.dependency_id).id << ")\n";
    });

    out << "(property 0 string \"viewLabel\"\n"
           "(default \"\" \"\")\n";
    std::for_each(mapping.begin(), mapping.end(), [&out](const std::pair<long long, Artifact>& node){
      out << "(node " << node.second.id << " \"" << node.second.name << "\")\n";
    });
    out << ")\n";

    out << "(property 0 color \"viewColor\"\n"
           "(default \"(255,95,95,255)\" \"(180,180,180,255)\")\n";
    std::for_each(mapping.begin(), mapping.end(), [&out](const std::pair<long long, Artifact>& node){
      out << "(node " << node.second.id << " \"" << tlp_fmt(node.second.color) << "\")\n";
    });
    out << ")\n";

    out << ")";

    return out;
  }
};

std::ostream& operator<<(std::ostream& out, const tlp_file_format& data) {
  return data(out);
}

struct dot_file_format {
  Database2& db;
  const std::set<Dependency>& dependencies;

  dot_file_format(Database2& db, const std::set<Dependency>& dependencies)
    : db(db), dependencies(dependencies) {}

  std::ostream& operator()(std::ostream& out) const {
    const std::map<long long, Artifact> mapping = map_artifacts(db, dependencies);

    out << "digraph g {\n";

    std::for_each(mapping.begin(), mapping.end(), [&out](const std::pair<long long, Artifact>& node){
      out << "\tn" << node.second.id << " [label=\"" << node.second.name << "\", color=\"" << hex_fmt(node.second.color) << "\"]\n";
    });

    std::for_each(dependencies.cbegin(), dependencies.cend(), [&out, &mapping] (const Dependency& dependency) {
      out << "\tn" << mapping.at(dependency.dependee_id).id << " -> n" << mapping.at(dependency.dependency_id).id << "\n";
    });

    out << "}";

    return out;
  }
};

std::ostream& operator<<(std::ostream& out, const dot_file_format& data) {
  return data(out);
}

} // anonymous namespace

boost::program_options::options_description Export_Dependencies_Command::options() const
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ("artifact",
       bpo::value<std::string>(),
       "Artifact to export.")
      ("format",
       bpo::value<std::string>()->default_value("tlp"),
       "Export format (tlp, dot).")
      ;

  return opt;
}

int Export_Dependencies_Command::execute(const std::vector<std::string>& args) const
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

  auto format = vm["format"].as<std::string>();

  if (vm.count("artifact") == 0) {
    std::set<Dependency> dependencies = get_all_dependencies(db,
                                                             vm["type"].as<std::vector<std::string>>(),
                                                             vm["not-type"].as<std::vector<std::string>>());

    if (format == "tlp") {
      std::cout << tlp_file_format(db, dependencies) << std::endl;
    } else if (format == "dot") {
      std::cout << dot_file_format(db, dependencies) << std::endl;
    }
  } else {
    long long id = db.artifact_id_by_name(vm["artifact"].as<std::string>());
    std::set<Dependency> dependencies = get_all_dependencies_for(db,
                                                                 id,
                                                                 vm["type"].as<std::vector<std::string>>(),
                                                                 vm["not-type"].as<std::vector<std::string>>());

    if (format == "tlp") {
      std::cout << tlp_file_format(db, dependencies) << std::endl;
    } else if (format == "dot") {
      std::cout << dot_file_format(db, dependencies) << std::endl;
    }
  }

  return 0;
}
