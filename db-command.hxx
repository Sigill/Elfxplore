#ifndef DBCOMMAND_HXX
#define DBCOMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class DB_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() const override;
  int execute(const std::vector<std::string>& args) const override;
};

#endif // DBCOMMAND_HXX
