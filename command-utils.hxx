#ifndef COMMANDUTILS_HXX
#define COMMANDUTILS_HXX

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <set>
#include <string>
#include <vector>

class Database2;

bool is_cc(const std::string& command);

class Command {
public:
  long long id = -1;
  std::string directory;
  std::string executable;
  std::string args;
};

class CompilationCommand : public Command {
public:
  long long artifact_id = -1;
  std::string output;
  std::string output_type;
};

namespace parse_command_options {
  constexpr int with_directory = 1 << 0;
  constexpr int expand_path    = 1 << 1;
};

void parse_command(const std::string& line, CompilationCommand& command, const int options = parse_command_options::with_directory | parse_command_options::expand_path);

void parse_commands(std::istream& in,
                    const std::function<void (size_t, const std::string&, const CompilationCommand&)>& notify);

void parse_compile_commands(std::istream& in,
                            const std::function<void(size_t, const std::string&, const CompilationCommand&)>& notify);

class CommandImporter
{
private:
  Database2& db;
  size_t count = 0UL;

public:
  CommandImporter(Database2& db)
   : db(db)
  {}

  void import_commands(std::istream& in);
  void import_compile_commands(std::istream& in);

  void reset_count() { count = 0; }
  size_t count_inserted() const { return count; }

protected:
  virtual void on_command(size_t item, const std::string& line, const CompilationCommand& command);
};

class Dependencies {
public:
  std::vector<std::string> files;
  std::vector<std::string> errors;
};

class DependenciesResolver {
public:
  std::vector<std::filesystem::path> library_directories;

  std::set<std::string> dependencies;
  std::vector<std::string> errors;

  void add_libraries_directory(const std::string& value);

  void locate_and_add_library(const std::string& namespec,
                              const std::vector<std::filesystem::path>& default_library_directories);
};

Dependencies parse_dependencies(const CompilationCommand& cmd,
                                const std::vector<std::filesystem::path>& default_library_directories);

std::string redirect_gcc_output(const CompilationCommand& command, const std::string& to = {});

std::string redirect_ar_output(const CompilationCommand& command, const std::string& to = {});

#endif // COMMANDUTILS_HXX
