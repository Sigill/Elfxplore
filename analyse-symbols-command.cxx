#include "analyse-symbols-command.hxx"

#include <iostream>
#include <iterator>
#include <iomanip>
#include <sstream>

#include <boost/program_options.hpp>

#include "infix_iterator.hxx"

#include "Database2.hxx"

namespace bpo = boost::program_options;

namespace {

struct quoted_string_literal
{
  const std::string& value;
  explicit quoted_string_literal(const std::string& value) : value(value) {}
};

quoted_string_literal
quote_string_literal(const std::string& value) {
  return quoted_string_literal(value);
}

std::ostream& operator<<(std::ostream& out, const quoted_string_literal& m) {
  return out << std::quoted(m.value.c_str(), '\'', '\'');
}

template<typename T>
struct joined
{
  const std::vector<T> values;
  const char* separator;
  explicit joined(const std::vector<T>& values, const char* separator)
    : values(values)
    , separator(separator)
  {}
};

template<typename T>
joined<T> join(const std::vector<T>& values, const char* separator) {
  return joined<T>(values, separator);
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const joined<T>& m) {
  if (m.values.size() > 1) std::copy(m.values.cbegin(), --m.values.cend(), std::ostream_iterator<T>(out, m.separator));
  if (m.values.size() > 0) out << m.values.back();
  return out;
}

template<>
std::ostream& operator<<(std::ostream& out, const joined<std::string>& m) {
  if (m.values.size() > 1) std::transform(m.values.cbegin(), --m.values.cend(),
                                          std::ostream_iterator<quoted_string_literal>(out, m.separator),
                                          quote_string_literal);
  if (m.values.size() > 0) out << quoted_string_literal(m.values.back());
  return out;
}

template<typename T>
struct in_expr_manip
{
  const std::vector<T> values;
  explicit in_expr_manip(std::vector<T> values)
    : values(std::move(values))
  {}
};

template<typename T>
in_expr_manip<T> in_expr(const std::vector<T>& values) {
  return in_expr_manip<T>(values);
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const in_expr_manip<T>& m) {
  return out << "(" << join(m.values, ", ") << ")";
}

} // anonymous namespace

int analyse_symbols_command(const std::vector<std::string>& command, const std::vector<std::string>& args)
{
  bpo::options_description desc("Options");

  desc.add_options()
      ("help,h", "Produce help message.")
      ("database,d", bpo::value<std::string>()->required(),
       "SQLite database.")
      ("artifact-type", bpo::value<std::vector<std::string>>()->multitoken(),
       "Only consider artifacts matching those types.")
      ("not-artifact-type", bpo::value<std::vector<std::string>>()->multitoken(),
       "Only consider artifacts not matching those types.")
      ("reference-category", bpo::value<std::vector<std::string>>()->multitoken(),
       "Only consider references matching those categories.")
      ("not-reference-category", bpo::value<std::vector<std::string>>()->multitoken(),
       "Only consider references not matching those categories.")
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

  std::vector<std::string> conditions;

  auto build_condition = [&vm, &conditions] (const char* expr, const char* arg) {
    if (vm.count(arg) > 0) {
      std::stringstream ss;
      ss << expr << " " << in_expr(vm[arg].as<std::vector<std::string>>());
      conditions.emplace_back(ss.str());
    }
  };

  build_condition("artifacts.type in", "artifact-type");
  build_condition("artifacts.type not in", "not-artifact-type");
  build_condition("symbol_references.category in", "reference-category");
  build_condition("symbol_references.category not in", "not-reference-category");

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

  SQLite::Statement duplicated_symbols_stm(db.database(), duplicated_symbols_query.str());

  while (duplicated_symbols_stm.executeStep()) {
    std::string dname = duplicated_symbols_stm.getColumn(2).getString();
    if (dname.empty())
      dname = duplicated_symbols_stm.getColumn(1).getString();

    std::cout << dname
              << ": occurences: " << duplicated_symbols_stm.getColumn(3).getInt64()
              << ", total size: " << duplicated_symbols_stm.getColumn(4).getInt64() << std::endl;
  }

  return 0;
}
