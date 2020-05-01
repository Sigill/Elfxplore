#ifndef ARTIFACTSCOMMAND_HXX
#define ARTIFACTSCOMMAND_HXX

#include "task.hxx"

class Artifacts_Task : public Task
{
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // ARTIFACTSCOMMAND_HXX
