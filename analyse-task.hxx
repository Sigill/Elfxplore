#ifndef ANALYSESYMBOLSTASK_HXX
#define ANALYSESYMBOLSTASK_HXX

#include "task.hxx"

class Analyse_Task : public Task {
private:
  unsigned int mNumThreads = 1;
public:
  using Task::Task;
  boost::program_options::options_description options() override;
  int execute(const std::vector<std::string>& args) override;
};

#endif // ANALYSESYMBOLSTASK_HXX
