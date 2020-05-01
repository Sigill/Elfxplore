#ifndef DEPENDENCIESCOMMAND_HXX
#define DEPENDENCIESCOMMAND_HXX

#include "task.hxx"

class Dependencies_Task : public Task {
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // DEPENDENCIESCOMMAND_HXX
