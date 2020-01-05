#ifndef COMMAND_HXX
#define COMMAND_HXX

#include <vector>
#include <string>
#include <boost/program_options.hpp>

class Command
{
private:
  std::vector<std::string> mCommand;
public:
  Command(const std::vector<std::string>& mCommand);
  void usage(std::ostream& out);
  virtual int execute(const std::vector<std::string>& args) = 0;

protected:
  bool mVerbose;
  boost::program_options::options_description default_options();

private:
  virtual boost::program_options::options_description options() = 0;
};

#endif // COMMAND_HXX
