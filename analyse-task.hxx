#ifndef ANALYSESYMBOLSTASK_HXX
#define ANALYSESYMBOLSTASK_HXX

#include "task.hxx"

class Analyse_Task : public Task {
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // ANALYSESYMBOLSTASK_HXX
