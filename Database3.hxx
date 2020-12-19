#ifndef DATABASE3_HXX
#define DATABASE3_HXX

#include "Database2.hxx"

#include <string>
#include <vector>

class CompilationCommand;

class Database3 : public Database2
{
public:
  explicit Database3(const std::string& storage);

  void import_command(const CompilationCommand& cmd);

  void load_dependencies();

  void load_symbols();
};

#endif // DATABASE3_HXX
