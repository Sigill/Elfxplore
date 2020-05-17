#include "extract-task.hxx"

#include <iostream>
#include <algorithm>
#include <functional>
#include <fstream>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <termcolor/termcolor.hpp>

#include "Database3.hxx"
#include "utils.hxx"
#include "command-utils.hxx"
#include "nm.hxx"
#include "logger.hxx"
#include "database-utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {



} // anonymous namespace

boost::program_options::options_description Extract_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("dependencies", "Extract dependencies from commands.")
      ("symbols", "Extract symbols from artifacts.")
      ;

  return opt;
}

int Extract_Task::execute(const std::vector<std::string>& args)
{
  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
    bpo::notify(vm);
  } catch(bpo::error &err) {
    std::cerr << err.what() << std::endl;
    return TaskStatus::ERROR;
  }

  if (vm.count("dependencies")) {
    db().load_dependencies();
  }

  if (vm.count("symbols")) {
    db().load_symbols();
  }

  return 0;
}
