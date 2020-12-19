#include "artifacts-task.hxx"

#include <iostream>

#include "Database3.hxx"
#include "query-utils.hxx"

namespace bpo = boost::program_options;

boost::program_options::options_description Artifacts_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ;

  return opt;
}

void Artifacts_Task::parse_args(const std::vector<std::string>& args)
{
  bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
  bpo::notify(vm);
}

void Artifacts_Task::execute(Database3& db)
{
  const std::vector<std::string> included_types = vm["type"].as<std::vector<std::string>>(),
                                 excluded_types = vm["not-type"].as<std::vector<std::string>>();

  std::stringstream ss;
  ss << "select name, type from artifacts";
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

  ss << " order by type asc, name asc";

  auto stm = db.statement(ss.str());
  while (stm.executeStep()) {
    std::cout << stm.getColumn(0).getString() << " : " << stm.getColumn(1).getString() << "\n";
  }

  std::flush(std::cout);
}
