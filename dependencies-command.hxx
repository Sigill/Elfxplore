#ifndef DEPENDENCIESCOMMAND_HXX
#define DEPENDENCIESCOMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class Dependencies_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // DEPENDENCIESCOMMAND_HXX
