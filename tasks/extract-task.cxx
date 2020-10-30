#include "extract-task.hxx"

#include <iostream>
#include <algorithm>
#include <functional>
#include <fstream>
#include <filesystem>

#include <termcolor/termcolor.hpp>

#include "Database3.hxx"
#include "utils.hxx"
#include "command-utils.hxx"
#include "nm.hxx"
#include "logger.hxx"
#include "database-utils.hxx"

namespace bpo = boost::program_options;
namespace fs = std::filesystem;

boost::program_options::options_description Extract_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("dependencies", "Extract dependencies from commands.")
      ("symbols", "Extract symbols from artifacts.")
      ;

  return opt;
}

void Extract_Task::parse_args(const std::vector<std::string>& args)
{
  bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
  bpo::notify(vm);
}

int Extract_Task::execute(Database3& db)
{
  if (vm.count("dependencies")) {
    db.load_dependencies();
  }

  if (vm.count("symbols")) {
    db.load_symbols();
  }

  return 0;
}
