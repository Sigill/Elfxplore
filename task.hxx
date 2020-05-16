#ifndef TASK_HXX
#define TASK_HXX

#include <iosfwd>
#include <string>
#include <vector>
#include <memory>

#include <boost/program_options/options_description.hpp>

class Database3;

enum TaskStatus {
  ERROR = -2,
  WRONG_ARGS = -1,
  SUCCESS = 0
};

class Task
{
private:
  bool mDryRun = true;
  std::shared_ptr<Database3> mDB;

public:
  Task();
  virtual ~Task() = default;

  virtual int execute(const std::vector<std::string>& args) = 0;

  void set_dryrun(bool b) { mDryRun = b; }
  void set_database(std::shared_ptr<Database3> db) { std::swap(db, mDB); }

  virtual boost::program_options::options_description options() = 0;

protected:
  bool dryrun() const { return mDryRun; }
  Database3& db() { return *mDB; }
};

#endif // COMMAND_HXX
