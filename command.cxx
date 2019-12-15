#include "command.hxx"

#include <iostream>

namespace bpo = boost::program_options;

Command::Command(const std::vector<std::string>& command)
  : mCommand(command)
{}

void Command::usage(std::ostream& out) const
{
  out << "Usage:";
  for(const std::string& c : mCommand)
    out << " " << c;
  out << " [options]" << std::endl;
  out << options();
}

boost::program_options::options_description Command::default_options() const
{
  bpo::options_description opts("Options");

  opts.add_options()
      ("help,h",
       "Produce help message.")
      ("db",
       bpo::value<std::string>()->required()->value_name("file"),
       "SQLite database.")
      ;

  return opts;
}
