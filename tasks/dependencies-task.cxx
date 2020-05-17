#include "dependencies-task.hxx"

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include <iterator>
#include <map>
#include <iomanip>
#include <tuple>
#include <utility>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <functional>

#include <SQLiteCpp/Statement.h>

#include "Database3.hxx"
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

void get_all_dependencies(Database2& db,
                          const std::vector<std::string>& included_types,
                          const std::vector<std::string>& excluded_types,
                          std::set<Dependency>& dependencies)
{
  std::ostringstream ss;
  ss << "select dependee_id, dependency_id from dependencies";
  if (!included_types.empty() || !excluded_types.empty())
    ss << " where";

  if (!included_types.empty()) {
    ss << " artifacts.type in " << in_expr(included_types);
  }

  if (!excluded_types.empty()) {
    if (!included_types.empty())
      ss << " and";

    ss << " artifacts.type not in " << in_expr(excluded_types);
  }

  auto stm = db.statement(ss.str());
  while (stm.executeStep()) {
    dependencies.emplace(stm.getColumn(0).getInt64(), stm.getColumn(1).getInt64());
  }
}

void get_dependencies_for(Database2& db,
                          long long artifact_id,
                          const std::vector<std::string>& included_types,
                          const std::vector<std::string>& excluded_types,
                          std::set<Dependency>& dependencies)
{
  SQLite::Statement dependencies_stm = db.build_get_depend_stm("dependency_id", "dependee_id", included_types, excluded_types);
  dependencies_stm.bind(1, artifact_id);
  for(long long dependency_id : Database2::get_ids(dependencies_stm)) {
    dependencies.emplace(artifact_id, dependency_id);
  }
}

void get_dependees_for(Database2& db,
                       long long artifact_id,
                       const std::vector<std::string>& included_types,
                       const std::vector<std::string>& excluded_types,
                       std::set<Dependency>& dependencies)
{
  SQLite::Statement dependees_stm = db.build_get_depend_stm("dependee_id", "dependency_id", included_types, excluded_types);
  dependees_stm.bind(1, artifact_id);

  for(long long dependee_id : Database2::get_ids(dependees_stm)) {
    dependencies.emplace(dependee_id, artifact_id);
  }
}

void get_all_dependencies_for(Database2& db,
                              long long artifact_id,
                              const std::vector<std::string>& included_types,
                              const std::vector<std::string>& excluded_types,
                              std::set<Dependency>& dependencies)
{
  SQLite::Statement dependencies_stm = db.build_get_depend_stm("dependency_id", "dependee_id", included_types, excluded_types);

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
}

void get_all_dependees_for(Database2& db,
                           long long artifact_id,
                           const std::vector<std::string>& included_types,
                           const std::vector<std::string>& excluded_types,
                           std::set<Dependency>& dependencies)
{
  SQLite::Statement dependees_stm = db.build_get_depend_stm("dependee_id", "dependency_id", included_types, excluded_types);

  std::set<long long> visited, queue = {artifact_id};

  while(!queue.empty()) {
    auto last = std::prev(queue.end());
    long long current_dependency_id = *last;
    queue.erase(last);

    if (visited.find(current_dependency_id) == visited.end()) {
      dependees_stm.bind(1, current_dependency_id);

      for(long long dependee_id : Database2::get_ids(dependees_stm)) {
        dependencies.emplace(dependee_id, current_dependency_id);

        queue.insert(dependee_id);
      }
    }

    visited.insert(current_dependency_id);
  }
}

struct ArtifactData {
  size_t id;
  std::string name;
  std::array<unsigned char, 3> color;

  ArtifactData(size_t id, std::string name, std::array<unsigned char, 3> color)
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

std::map<long long, ArtifactData> map_artifacts(Database2& db, const std::set<Dependency>& dependencies, const bool path)
{
  std::map<long long, ArtifactData> mapping;

  SQLite::Statement q = db.statement(build_artifacts_query(list_artifacts(dependencies)));

  size_t i = 0;
  while (q.executeStep()) {
    std::string label = q.getColumn(1).getString();
    if (path == false)
      label = bfs::path(label).filename().string();

    mapping.emplace(std::piecewise_construct,
                    std::forward_as_tuple(q.getColumn(0).getInt64()),
                    std::forward_as_tuple(i++,
                                          label,
                                          node_color.at(q.getColumn(2).getString())
                                          )
                    );
  }

  return mapping;
}

struct tlp_file_format {
  const std::map<long long, ArtifactData>& artifacts;
  const std::set<Dependency>& dependencies;

  tlp_file_format(const std::map<long long, ArtifactData>& artifacts, const std::set<Dependency>& dependencies)
    : artifacts(artifacts), dependencies(dependencies) {}

  std::ostream& operator()(std::ostream& out) const {
    out << "(tlp \"2.3\"\n";

    std::cout << "(nb_nodes " << artifacts.size() << ")\n";
    std::cout << "(nodes 0.." << (artifacts.size() - 1) << ")\n";

    std::cout << "(nb_edges " << dependencies.size() << ")\n";

    std::for_each(dependencies.cbegin(), dependencies.cend(), [i=0ul, &out, this] (const Dependency& dependency) mutable {
      out << "(edge " << i++ << " " << artifacts.at(dependency.dependee_id).id << " " << artifacts.at(dependency.dependency_id).id << ")\n";
    });

    out << "(property 0 string \"viewLabel\"\n"
           "(default \"\" \"\")\n";
    std::for_each(artifacts.begin(), artifacts.end(), [&out](const std::pair<long long, ArtifactData>& node){
      out << "(node " << node.second.id << " \"" << node.second.name << "\")\n";
    });
    out << ")\n";

    out << "(property 0 color \"viewColor\"\n"
           "(default \"(255,95,95,255)\" \"(180,180,180,255)\")\n";
    std::for_each(artifacts.begin(), artifacts.end(), [&out](const std::pair<long long, ArtifactData>& node){
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
  const std::map<long long, ArtifactData>& artifacts;
  const std::set<Dependency>& dependencies;

  dot_file_format(const std::map<long long, ArtifactData>& artifacts, const std::set<Dependency>& dependencies)
    : artifacts(artifacts), dependencies(dependencies) {}

  std::ostream& operator()(std::ostream& out) const {
    out << "digraph g {\n"
        << "\tnode [style=filled]\n";

    std::for_each(artifacts.begin(), artifacts.end(), [&out](const std::pair<long long, ArtifactData>& node){
      out << "\tn" << node.second.id << " [label=\"" << node.second.name << "\", fillcolor=\"" << hex_fmt(node.second.color) << "\"]\n";
    });

    std::for_each(dependencies.cbegin(), dependencies.cend(), [&out, this] (const Dependency& dependency) {
      out << "\tn" << artifacts.at(dependency.dependee_id).id << " -> n" << artifacts.at(dependency.dependency_id).id << "\n";
    });

    out << "}";

    return out;
  }
};

std::ostream& operator<<(std::ostream& out, const dot_file_format& data) {
  return data(out);
}

struct txt_format {
  const std::map<long long, ArtifactData>& artifacts;
  const std::set<Dependency>& dependencies;

  txt_format(const std::map<long long, ArtifactData>& artifacts, const std::set<Dependency>& dependencies)
    : artifacts(artifacts), dependencies(dependencies) {}

  std::ostream& operator()(std::ostream& out) const {
    std::for_each(dependencies.cbegin(), dependencies.cend(), [&out, this] (const Dependency& dependency) {
      out << artifacts.at(dependency.dependee_id).name << " -> " << artifacts.at(dependency.dependency_id).name << "\n";
    });

    return out;
  }
};

std::ostream& operator<<(std::ostream& out, const txt_format& data) {
  return data(out);
}

void print(const std::map<long long, ArtifactData>& artifacts, const std::set<Dependency>& dependencies, std::ostream& out, const std::string& format)
{
  if (format == "tlp") {
    out << tlp_file_format(artifacts, dependencies) << std::endl;
  } else if (format == "dot") {
    out << dot_file_format(artifacts, dependencies) << std::endl;
  } else if (format == "txt") {
    out << txt_format(artifacts, dependencies) << std::endl;
  }
}

} // anonymous namespace

boost::program_options::options_description Dependencies_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ("artifact",
       bpo::value<std::vector<std::string>>()->multitoken(),
       "Artifact to export.")
      ("format",
       bpo::value<std::string>()->default_value("txt"),
       "Export format: txt (default), tlp, dot.")
      ("dependencies", "Export dependencies.")
      ("dependees", "Export dependees.")
      ("full-path", "Print full path.")
      ("follow,f", "Follow dependencies.")
      ;

  return opt;
}

int Dependencies_Task::execute(const std::vector<std::string>& args)
{
  bpo::positional_options_description p;
  p.add("artifact", -1);

  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(options()).positional(p).run(), vm);
    bpo::notify(vm);
  } catch(bpo::error &err) {
    std::cerr << err.what() << std::endl;
    return TaskStatus::ERROR;
  }

  const std::string format = vm["format"].as<std::string>();

  if (!(format == "txt" || format == "tlp" || format == "dot")) {
    std::cerr << "Invalid format: " << format << std::endl;
    return TaskStatus::WRONG_ARGS;
  }

  const bool follow = vm.count("follow") > 0;

  const std::vector<std::string> included_types = vm["type"].as<std::vector<std::string>>(),
                                 excluded_types = vm["not-type"].as<std::vector<std::string>>();

  std::set<Dependency> dependencies;

  db().load_dependencies();

  if (vm.count("artifact") == 0) {
    get_all_dependencies(db(), included_types, excluded_types, dependencies);
  } else {
    const bool export_dependencies = vm.count("dependencies") == vm.count("dependees") || vm.count("dependencies") == 1;
    const bool export_dependees = vm.count("dependencies") == vm.count("dependees") || vm.count("dependees") == 1;

    for(const std::string& artifact : vm["artifact"].as<std::vector<std::string>>())
    {
      const long long id = db().artifact_id_by_name(artifact);

      if (follow) {
        if (export_dependencies)
          get_all_dependencies_for(db(), id, included_types, excluded_types, dependencies);

        if (export_dependees)
          get_all_dependees_for(db(), id, included_types, excluded_types, dependencies);
      } else {
        if (export_dependencies)
          get_dependencies_for(db(), id, included_types, excluded_types, dependencies);

        if (export_dependees)
          get_dependees_for(db(), id, included_types, excluded_types, dependencies);
      }
    }
  }

  const std::map<long long, ArtifactData> artifacts = map_artifacts(db(), dependencies, vm.count("full-path") > 0);

  print(artifacts, dependencies, std::cout, format);

  return TaskStatus::SUCCESS;
}
