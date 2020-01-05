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

std::map<long long, std::set<long long>> find_undefined_symbols(Database2& db, const std::vector<std::string>& artifacts)
{
  std::stringstream ss;
  ss << R"(
select artifact_id, symbol_id
from symbol_references
inner join artifacts on artifacts.id = artifact_id
where category = "undefined"
)";

  if (artifacts.empty()) {
    ss << R"(and artifacts.type in ("shared", "library", "executable"))" << "\n";
  } else {
    ss << "and artifacts.name in " << in_expr(artifacts) << "\n";
  }

  SQLite::Statement stm = db.statement(ss.str());

  std::map<long long, std::set<long long>> artifact_undefined_symbols;
  while(stm.executeStep()) {
    artifact_undefined_symbols[stm.getColumn(0).getInt64()].insert(stm.getColumn(1).getInt64());
  }

  return artifact_undefined_symbols;
}

template<typename K, typename V, typename C, typename A>
std::vector<K> map_keys(const std::map<K, V, C, A>& map) {
  std::vector<K> v(map.size());
  std::transform(map.begin(), map.end(), v.begin(),
                 [](const std::pair<K, V>& pair) -> K { return pair.first; });
  return v;
}

std::map<std::string, long long> get_symbol_hnames(Database2& db, const std::vector<long long>& ids)
{
  std::map<std::string, long long> names;

  std::stringstream ss;
  ss << R"(
select id, name, dname
from symbols
where id in )" << in_expr(ids);

  SQLite::Statement stm = db.statement(ss.str());

  while(stm.executeStep()) {
    names.emplace(
          symbol_hname(stm.getColumn(1).getString(), stm.getColumn(2).getString()),
          stm.getColumn(0).getInt64()
          );
  }

  return names;
}

void analyse_undefined_symbols(Database2& db, const std::vector<std::string>& artifacts)
{
  std::map<long long, std::set<long long>> artifact_undefined_symbols = find_undefined_symbols(db, artifacts);

  for (std::pair<const long long, std::set<long long>>& it : artifact_undefined_symbols) {
    const long long artifact_id = it.first;
    std::set<long long>& undefined_symbols = it.second;

    std::stringstream ss;
    ss << R"(
select symbol_id
from symbol_references
inner join dependencies on symbol_references.artifact_id = dependencies.dependency_id
where symbol_references.category = "external"
and dependencies.dependee_id = ?
and symbol_references.symbol_id in )" << in_expr(std::vector<long long>(undefined_symbols.begin(), undefined_symbols.end()));

    SQLite::Statement stm = db.statement(ss.str());
    stm.bind(1, artifact_id);

    while(stm.executeStep()) {
      undefined_symbols.erase(stm.getColumn(0).getInt64());
    }

    std::cout << "############ " << db.artifact_name_by_id(artifact_id) << "\n";
    for(const std::pair<std::string, long long>& undefined_symbol : get_symbol_hnames(db, {undefined_symbols.begin(), undefined_symbols.end()})) {
      std::cout << undefined_symbol.first << "\n";
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

  if (vm.count("duplicated-symbols") + vm.count("undefined-symbols") != 1) {
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
    analyse_undefined_symbols(db, vm["artifact"].as<std::vector<std::string>>());
  }

  return 0;
}
