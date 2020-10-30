#include "db-task.hxx"

#include <iostream>

#include "Database3.hxx"

namespace bpo = boost::program_options;

boost::program_options::options_description DB_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("clear-symbols", "Clear the symbols table.")
      ("optimize", "Optimize database.")
      ("vacuum", "Vacuum (compact) database.");

  return opt;
}

void DB_Task::parse_args(const std::vector<std::string>& args)
{
  bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
  bpo::notify(vm);
}

int DB_Task::execute(Database3& db)
{
  if (vm.count("clear-symbols")) {
    db.truncate_symbols();
  }

  if (vm.count("optimize")) {
    db.optimize();
  }

  if (vm.count("vacuum")) {
    db.vacuum();
  }

  return TaskStatus::SUCCESS;
}
