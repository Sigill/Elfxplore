#ifndef EXTRACT_COMMAND_HXX
#define EXTRACT_COMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class Extract_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // EXTRACT_COMMAND_HXX
