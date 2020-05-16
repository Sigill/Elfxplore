#ifndef DATABASEUTILS_HXX
#define DATABASEUTILS_HXX

#include <vector>
#include <string>
#include <functional>
#include <iosfwd>
#include <set>

class Database2;
class CompilationCommand;
class Artifact;

void import_command(Database2& db,
                    const std::string& line,
                    const std::function<void(const std::string&, const CompilationCommand&)>& notify);

void import_commands(Database2& db,
                     std::istream& in,
                     const std::function<void(const std::string&, const CompilationCommand&)>& notify);

using DependenciesCallback = void(const CompilationCommand&, const std::vector<Artifact>&, const std::vector<std::string>&);

void extract_dependencies(Database2& db,
                          const std::function<DependenciesCallback>& notify);

#endif // DATABASEUTILS_HXX
