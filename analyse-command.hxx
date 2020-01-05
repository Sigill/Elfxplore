#ifndef ANALYSESYMBOLSCOMMAND_HXX
#define ANALYSESYMBOLSCOMMAND_HXX

#include <string>
#include <vector>

#include "command.hxx"

class Analyse_Command : public Command {
public:
  using Command::Command;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // ANALYSESYMBOLSCOMMAND_HXX
