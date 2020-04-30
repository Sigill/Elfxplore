#ifndef EXTRACT_TASK_HXX
#define EXTRACT_TASK_HXX

#include <vector>
#include <string>

#include "task.hxx"

class Extract_Task : public Task {
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // EXTRACT_TASK_HXX
