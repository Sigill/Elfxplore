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
  void usage(std::ostream& out) const;
  virtual int execute(const std::vector<std::string>& args) const = 0;

protected:
  boost::program_options::options_description default_options() const;

private:
  virtual boost::program_options::options_description options() const = 0;
};

#endif // COMMAND_HXX
