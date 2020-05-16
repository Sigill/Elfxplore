#ifndef DATABASEUTILS_HXX
#define DATABASEUTILS_HXX

#include <vector>
#include <string>
#include <functional>
#include <iosfwd>

class Database2;
class CompilationCommand;

void import_command(Database2& db,
                    const std::string& line,
                    const std::function<void(const std::string&, const CompilationCommand&)>& notify);

void import_commands(Database2& db,
                     std::istream& in,
                     const std::function<void(const std::string&, const CompilationCommand&)>& notify);


#endif // DATABASEUTILS_HXX
