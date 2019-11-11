#include "db-command.hxx"

#include <iostream>
#include <boost/program_options.hpp>

#include "Database2.hxx"

namespace bpo = boost::program_options;

int db_command(const std::vector<std::string>& command, const std::vector<std::string>& args)
{
  bpo::options_description desc("Options");

  desc.add_options()
      ("help,h", "Produce help message.")
      ("db", bpo::value<std::string>()->required()->value_name("file"),
       "SQLite database.")
      ("init", "Initialize the database.")
      ("clear-symbols", "Clear the symbols table.");

  bpo::positional_options_description p;
  p.add("db", 1);

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

  Database2 db(vm["db"].as<std::string>());

  if (vm.count("init")) {
    db.create();
  }

  if (vm.count("clear-symbols")) {
    db.truncate_symbols();
  }

  return 0;
}
