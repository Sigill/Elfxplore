#ifndef ARTIFACTSCOMMAND_HXX
#define ARTIFACTSCOMMAND_HXX

#include <vector>
#include <string>

#include "command.hxx"

class ArtifactsCommand : public Command
{
public:
  using Command::Command;
  boost::program_options::options_description options() const override;
  int execute(const std::vector<std::string>& args) const override;
};

#endif // ARTIFACTSCOMMAND_HXX
