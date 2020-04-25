#include "command.hxx"

#include <iostream>

#include "logger.hxx"

namespace bpo = boost::program_options;

namespace {

std::istream& operator>>(std::istream& in, logger::severity_level& level) {
  std::string token;
  in >> token;
  if (token == "trace")
    level = logger::trace;
  else if (token == "debug")
    level = logger::debug;
  else if (token == "info")
    level = logger::info;
  else if (token == "warning")
    level = logger::warning;
  else if (token == "error")
    level = logger::error;
  else if (token == "fatal")
    level = logger::fatal;
  else throw boost::program_options::invalid_option_value("Invalid severity level");
  return in;
}

void set_log_level(const logger::severity_level& level) {
  logger::_severity_level = level;
}
} // anonymous namespace

Command::Command(const std::vector<std::string>& command)
  : mCommand(command)
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
       bpo::value<logger::severity_level>()->default_value(logger::fatal)->notifier(set_log_level),
       "Verbosity level (trace, debug, info, warning, error, fatal).")
      ("db",
       bpo::value<std::string>()->required()->value_name("file"),
       "SQLite database.")
      ;

  return opts;
}
