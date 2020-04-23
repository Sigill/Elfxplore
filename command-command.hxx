#ifndef COMMANDCOMMAND_HXX
#define COMMANDCOMMAND_HXX

#include "command.hxx"

class Command_Command : public Command
{
public:
  using Command::Command;

  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // COMMANDCOMMAND_HXX
