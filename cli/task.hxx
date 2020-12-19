#ifndef TASK_HXX
#define TASK_HXX

#include <iosfwd>
#include <string>
#include <vector>
#include <memory>

#include <boost/program_options/options_description.hpp>

class Database3;

class Task
{
private:
  std::shared_ptr<Database3> mDB;

public:
  Task();
  virtual ~Task() = default;

  virtual boost::program_options::options_description options() = 0;

  virtual void parse_args(const std::vector<std::string>& args) = 0;

  virtual void execute(Database3& db) = 0;
};

#endif // COMMAND_HXX
