#ifndef ARTIFACTSCOMMAND_HXX
#define ARTIFACTSCOMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class ArtifactsCommand : public Command
{
public:
  using Command::Command;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // ARTIFACTSCOMMAND_HXX
