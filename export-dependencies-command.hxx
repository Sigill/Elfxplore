#ifndef EXPORTDEPENDENCIESCOMMAND_HXX
#define EXPORTDEPENDENCIESCOMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class Export_Dependencies_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() const override;
  int execute(const std::vector<std::string>& args) const override;
};

#endif // EXPORTDEPENDENCIESCOMMAND_HXX
