#include "command.hxx"

#include <iostream>

namespace bpo = boost::program_options;

Command::Command(const std::vector<std::string>& command)
  : mCommand(command)
  , mVerbose(false)
{}

void Command::usage(std::ostream& out)
{
  out << "Usage:";
  for(const std::string& c : mCommand)
    out << " " << c;
  out << " [options]" << std::endl;
  out << options();
}

boost::program_options::options_description Command::default_options()
{
  bpo::options_description opts("Options");

  opts.add_options()
      ("help,h",
       "Produce help message.")
      ("verbose,v",
       bpo::bool_switch(&mVerbose)->default_value(false),
       "Verbose mode.")
      ("db",
       bpo::value<std::string>()->required()->value_name("file"),
       "SQLite database.")
      ;

  return opts;
}
