#ifndef TASK_HXX
#define TASK_HXX

#include <iosfwd>
#include <string>
#include <vector>
#include <boost/program_options/options_description.hpp>

class Task
{
private:
  std::vector<std::string> mCommand;
  bool mDryRun = false;

public:
  Task(const std::vector<std::string>& mCommand);
  virtual ~Task() = default;

  void usage(std::ostream& out);
  virtual int execute(const std::vector<std::string>& args) = 0;

  bool dryrun() const { return mDryRun; }

protected:
  boost::program_options::options_description default_options();

private:
  virtual boost::program_options::options_description options() = 0;
};

#endif // COMMAND_HXX
