#ifndef COMMANDTASK_HXX
#define COMMANDTASK_HXX

#include "task.hxx"

class Command_Task : public Task
{
public:
  using Task::Task;

  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // COMMANDTASK_HXX
