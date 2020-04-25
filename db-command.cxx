#include "db-command.hxx"

#include <iostream>
#include <boost/program_options.hpp>

#include "Database2.hxx"

namespace bpo = boost::program_options;

boost::program_options::options_description DB_Command::options()
{
  bpo::options_description opt = default_options();
  opt.add_options()
      ("init", "Initialize the database.")
      ("clear-symbols", "Clear the symbols table.")
      ("optimize", "Optimize database.")
      ("vacuum", "Vacuum (compact) database.");

  return opt;
}

int DB_Command::execute(const std::vector<std::string>& args)
{
  bpo::positional_options_description p;
  p.add("db", 1);

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

  Database2 db(vm["db"].as<std::string>());

  if (vm.count("init")) {
    db.create();
  }

  if (vm.count("clear-symbols")) {
    db.truncate_symbols();
  }

  if (vm.count("optimize")) {
    db.optimize();
  }

  if (vm.count("vacuum")) {
    db.vacuum();
  }

  return 0;
}
