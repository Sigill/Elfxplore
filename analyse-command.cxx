#include "analyse-command.hxx"

#include <iostream>
#include <sstream>
#include <algorithm>

#include <boost/program_options.hpp>

#include "infix_iterator.hxx"

#include "Database2.hxx"
#include "query-utils.hxx"

namespace bpo = boost::program_options;

namespace {

std::string symbol_hname(std::string name, std::string dname) {
  return dname.empty() ? name : dname;
}

void analyse_duplicated_symbols(Database2& db,
                                const std::vector<std::string>& included_types,
                                const std::vector<std::string>& excluded_types,
                                const std::vector<std::string>& included_categories,
                                const std::vector<std::string>& excluded_categories)
{
  std::vector<std::string> conditions;

  auto build_condition = [&conditions] (const char* expr, const std::vector<std::string>& values) {
    if (!values.empty()) {
      std::stringstream ss;
      ss << expr << " " << in_expr(values);
      conditions.emplace_back(ss.str());
    }
  };

  build_condition("artifacts.type in", included_types);
  build_condition("artifacts.type not in", excluded_types);
  build_condition("symbol_references.category in", included_categories);
  build_condition("symbol_references.category not in", excluded_categories);

  std::stringstream duplicated_symbols_query;
  duplicated_symbols_query << R"(
select symbols.id, symbols.name as name, symbols.dname as dname, count(symbol_references.id) as occurences, sum(symbol_references.size) as total_size
from symbols
inner join symbol_references on symbols.id = symbol_references.symbol_id
inner join artifacts on artifacts.id = symbol_references.artifact_id
where symbol_references.size > 0
)";

  if (!conditions.empty()) {
    duplicated_symbols_query << "and ";
    std::copy(conditions.cbegin(), conditions.cend(), infix_ostream_iterator<std::string>(duplicated_symbols_query, "\nand "));
  }

  duplicated_symbols_query << R"(
group by symbols.id
having occurences > 1
order by total_size desc, name asc;
)";

  std::cout << duplicated_symbols_query.str() << std::endl;

  SQLite::Statement duplicated_symbols_stm = db.statement(duplicated_symbols_query.str());

  while (duplicated_symbols_stm.executeStep()) {
    std::cout << symbol_hname(duplicated_symbols_stm.getColumn(1).getString(), duplicated_symbols_stm.getColumn(2).getString())
              << ": occurences: " << duplicated_symbols_stm.getColumn(3).getInt64()
              << ", total size: " << duplicated_symbols_stm.getColumn(4).getInt64() << std::endl;
  }
}

template<typename K, typename V, typename C, typename A>
std::vector<K> map_keys(const std::map<K, V, C, A>& map) {
  std::vector<K> v(map.size());
  std::transform(map.begin(), map.end(), v.begin(),
                 [](const std::pair<K, V>& pair) -> K { return pair.first; });
  return v;
}

std::map<long long, std::string> get_symbol_hnames(Database2& db, const std::vector<long long>& ids)
{
  std::map<long long, std::string> names;

  std::stringstream ss;
  ss << R"(
select id, name, dname
from symbols
where id in )" << in_expr(ids);

  SQLite::Statement stm = db.statement(ss.str());

  while(stm.executeStep()) {
    names.emplace(
          stm.getColumn(0).getInt64(),
          symbol_hname(stm.getColumn(1).getString(), stm.getColumn(2).getString())
          );
  }

  return names;
}

std::set<long long> find_unresolved_symbols(Database2& db, const long long artifact_id)
{
  const std::vector<long long> undefined_symbols = db.undefined_symbols(artifact_id);
  std::set<long long> unresolved_symbols(undefined_symbols.cbegin(), undefined_symbols.cend());

  std::stringstream ss;
  ss << R"(
select symbol_id
from symbol_references
inner join dependencies on symbol_references.artifact_id = dependencies.dependency_id
where symbol_references.category = "external"
and dependencies.dependee_id = ?
and symbol_references.symbol_id in )" << in_expr(undefined_symbols);

  SQLite::Statement stm = db.statement(ss.str());
  stm.bind(1, artifact_id);

  while(stm.executeStep()) {
    unresolved_symbols.erase(stm.getColumn(0).getInt64());
  }

  return unresolved_symbols;
}

template<typename T>
std::vector<T> as_vector(const std::set<T>& s)
{
  return std::vector<T>(s.cbegin(), s.cend());
}

void analyse_undefined_symbols(Database2& db, const std::vector<long long>& artifacts)
{
  for(const long long artifact_id : artifacts) {
    const std::vector<long long> undefined_symbols = as_vector(find_unresolved_symbols(db, artifact_id));

    if (!undefined_symbols.empty()) {
      const std::map<long long, std::vector<std::string>> resolving_artifacts = db.resolve_symbols(undefined_symbols);

      std::cout << db.artifact_name_by_id(artifact_id) << "\n";

      for(const std::pair<long long, std::string>& undefined_symbol : get_symbol_hnames(db, undefined_symbols)) {
        std::cout << "\t" << undefined_symbol.second;

        auto where_resolved = resolving_artifacts.find(undefined_symbol.first);
        if (where_resolved != resolving_artifacts.cend()) {
          std::cout << " -> ";
          std::copy(where_resolved->second.cbegin(), where_resolved->second.cend(), infix_ostream_iterator<std::string>(std::cout, ", "));
        }
        std::cout << "\n";
      }
    }
  }
}

std::vector<std::string> get_useless_dependencies(Database2& db,
                                                  const long long dependee_id,
                                                  const std::vector<long long>& useful_dependencies)
{
  std::stringstream useless_dependencies_q;
  useless_dependencies_q << R"(
select artifacts.name
from artifacts
inner join dependencies on dependencies.dependency_id = artifacts.id
where artifacts.type = "shared"
and dependencies.dependee_id = ?
and dependencies.dependency_id not in )" << in_expr(useful_dependencies);

  SQLite::Statement useless_dependencies_stm = db.statement(useless_dependencies_q.str());
  useless_dependencies_stm.bind(1, dependee_id);

  std::vector<std::string> useless_dependencies;

  while(useless_dependencies_stm.executeStep()) {
    useless_dependencies.emplace_back(useless_dependencies_stm.getColumn(0).getString());
  }

  return useless_dependencies;
}

std::vector<long long> get_shared_dependencies(Database2& db, const long long dependee_id)
{
  SQLite::Statement dependencies_stm = db.build_get_depend_stm("dependency_id", "dependee_id", {"shared"}, {});
  dependencies_stm.bind(1, dependee_id);
  return Database2::get_ids(dependencies_stm);
}

std::vector<long long> get_useful_dependencies_simple1(Database2& db, const long long dependee_id)
{
  std::stringstream useful_dependencies_q;
  useful_dependencies_q << R"(
select distinct symbol_references.artifact_id
from symbol_references
where symbol_references.artifact_id in)" << in_expr(get_shared_dependencies(db, dependee_id)) << R"(
and symbol_references.category = "external"
and symbol_references.symbol_id in )" << in_expr(db.undefined_symbols(dependee_id));

  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
  return Database2::get_ids(useful_dependencies_stm);
}

//std::vector<long long> get_useful_dependencies_simple2(Database2& db, const long long dependee_id)
//{
//  const std::vector<long long> undefined_symbols = get_undefined_symbols(db, dependee_id);

//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct symbol_references.artifact_id
//from symbol_references
//inner join dependencies on dependencies.dependency_id = symbol_references.artifact_id
//inner join artifacts on artifacts.id = symbol_references.artifact_id
//where dependencies.dependee_id = ?
//and artifacts.type = "shared"
//and symbol_references.category = "external"
//and symbol_references.symbol_id in )" << in_expr(undefined_symbols);

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

//std::vector<long long> get_useful_dependencies_join(Database2& db, const long long dependee_id)
//{
//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct dependencies.dependency_id
//from dependencies
//inner join symbol_references as external_references on external_references.artifact_id = dependencies.dependency_id
//inner join symbol_references as undefined_references on undefined_references.artifact_id = dependencies.dependee_id
//                                                  and undefined_references.symbol_id = external_references.symbol_id
//inner join artifacts /*indexed by artifact_by_type*/ on artifacts.id = dependencies.dependency_id
//where external_references.category = "external"
//and undefined_references.category = "undefined"
//and artifacts.type = "shared"
//and dependencies.dependee_id = ?)";

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

//std::vector<long long> get_useful_dependencies_inner_query(Database2& db, const long long dependee_id)
//{
//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct dependencies.dependee_id, dependencies.dependency_id
//from dependencies
//inner join artifacts /*indexed by artifact_by_type*/ on artifacts.id = dependencies.dependency_id
//where artifacts.type = "shared"
//and dependencies.dependee_id = ?
//and not exists (
//select 1
//from symbol_references as undefined_references
//inner join symbol_references as external_references on external_references.artifact_id = dependencies.dependency_id
//                                                 and undefined_references.symbol_id = external_references.symbol_id
//where undefined_references.artifact_id = dependencies.dependee_id
//and external_references.category = "external"
//and undefined_references.category = "undefined"
//)
//)";

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

std::vector<long long> get_generated_shared_libs_and_executables(Database2& db, const std::vector<std::string>& selection)
{
  std::vector<long long> artifacts;

  std::stringstream ss;
  ss << R"(
select id
from artifacts
where generating_command_id != -1
)";

  if (selection.empty()) {
    ss << R"(and artifacts.type in ("shared", "executable"))" << "\n";
  } else {
    ss << "and artifacts.name in " << in_expr(selection) << "\n";
  }

  SQLite::Statement stm = db.statement(ss.str());

  while(stm.executeStep()) {
    artifacts.emplace_back(stm.getColumn(0));
  }

  return artifacts;
}

void analyse_useless_dependencies(Database2& db, const std::vector<long long>& artifacts)
{
  for(const long long artifact_id : artifacts) {
    const std::vector<long long> useful_dependencies = get_useful_dependencies_simple1(db, artifact_id);

    const std::vector<std::string> useless_dependencies = get_useless_dependencies(db, artifact_id, useful_dependencies);

    if (!useless_dependencies.empty()) {
      std::cout << db.artifact_name_by_id(artifact_id) << "\n";
      for(const std::string& useless_dependency : useless_dependencies) {
        std::cout << "\t" << useless_dependency << "\n";
      }
    }
  }
}

} // anonymous namespace

boost::program_options::options_description Analyse_Command::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ("category",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider references matching those categories.")
      ("not-category",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider references not matching those categories.")
      ("artifact",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Artifact to export.")
      ("duplicated-symbols", "Analyse duplicated symbols.")
      ("undefined-symbols", "Analyse undefined symbols.")
      ("useless-dependencies", "Analyse useless dependencies.")
      ;

  return opt;
}

int Analyse_Command::execute(const std::vector<std::string>& args)
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

  if (vm.count("duplicated-symbols") + vm.count("undefined-symbols") + vm.count("useless-dependencies")!= 1) {
    std::cerr << "Invalid analytis type" << std::endl;
    return -1;
  }

  if (vm.count("duplicated-symbols")) {
    analyse_duplicated_symbols(db,
                               vm["type"].as<std::vector<std::string>>(),
                               vm["not-type"].as<std::vector<std::string>>(),
                               vm["category"].as<std::vector<std::string>>(),
                               vm["not-category"].as<std::vector<std::string>>());
  } else if (vm.count("undefined-symbols")) {
    const std::vector<long long> artifacts = get_generated_shared_libs_and_executables(db, vm["artifact"].as<std::vector<std::string>>());
    analyse_undefined_symbols(db, artifacts);
  } else if (vm.count("useless-dependencies")) {
    const std::vector<long long> artifacts = get_generated_shared_libs_and_executables(db, vm["artifact"].as<std::vector<std::string>>());
    analyse_useless_dependencies(db, artifacts);
  }

  return 0;
}
