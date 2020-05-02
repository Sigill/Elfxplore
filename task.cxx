#include "task.hxx"

#include <iostream>

#include <boost/program_options.hpp>

#include "logger.hxx"

namespace bpo = boost::program_options;

namespace logger {

void validate(boost::any& v,
              const std::vector<std::string>& values,
              ::logger::severity_level* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  const std::string& s = bpo::validators::get_single_string(values);

  if (s == "trace")
    v = boost::any(logger::trace);
  else if (s == "debug")
    v = boost::any(logger::debug);
  else if (s == "info")
    v = boost::any(logger::info);
  else if (s == "warning")
    v = boost::any(logger::warning);
  else if (s == "error")
    v = boost::any(logger::error);
  else if (s == "fatal")
    v = boost::any(logger::fatal);
  else
    throw bpo::invalid_option_value(s);
}

} // anonymous namespace



Task::Task(const std::vector<std::string>& command)
  : mCommand(command)
{}

void Task::usage(std::ostream& out)
{
  out << "Usage:";
  for(const std::string& c : mCommand)
    out << " " << c;
  out << " [options]" << std::endl;
  out << options();
}

boost::program_options::options_description Task::default_options()
{
  bpo::options_description opts("Options");

  opts.add_options()
      ("help,h",
       "Produce help message.")
      ("verbose,v",
       bpo::value<logger::severity_level>(&::logger::_severity_level)->default_value(logger::fatal, "fatal"),
       "Verbosity level (trace, debug, info, warning, error, fatal).")
      ("dry-run,n",
       bpo::bool_switch(&mDryRun),
       "Do not write anything to the database.")
      ("db",
       bpo::value<std::string>()->required()->value_name("file"),
       "SQLite database.")
      ;

  return opts;
}
