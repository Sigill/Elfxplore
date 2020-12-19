#ifndef ARTIFACTSCOMMAND_HXX
#define ARTIFACTSCOMMAND_HXX

#include "task.hxx"

#include <boost/program_options.hpp>

class Artifacts_Task : public Task
{
private:
  boost::program_options::variables_map vm;

public:
  using Task::Task;
  boost::program_options::options_description options() override;
  void parse_args(const std::vector<std::string>& args) override;
  void execute(Database3& db) override;
};

#endif // ARTIFACTSCOMMAND_HXX
