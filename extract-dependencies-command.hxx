#ifndef ANALYSE_DEPENDENCIES_COMMAND_HXX
#define ANALYSE_DEPENDENCIES_COMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class Extract_Dependencies_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() const override;
  int execute(const std::vector<std::string>& args) const override;
};

#endif // ANALYSE_DEPENDENCIES_COMMAND_HXX
