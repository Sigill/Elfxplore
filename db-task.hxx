#ifndef DBTASK_HXX
#define DBTASK_HXX

#include <vector>
#include <string>

#include "task.hxx"

class DB_Task : public Task {
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // DBTASK_HXX
