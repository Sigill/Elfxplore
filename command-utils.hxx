#ifndef COMMANDUTILS_HXX
#define COMMANDUTILS_HXX

#include <string>
#include <vector>
#include <set>

#include <boost/filesystem/path.hpp>

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

  bool is_complete() const;
};

void parse_command(const std::string& line, CompilationCommand& command);

class Dependencies {
public:
  std::vector<std::string> files;
  std::vector<std::string> errors;
};

class DependenciesResolver {
public:
  std::vector<boost::filesystem::path> library_directories;

  std::set<std::string> dependencies;
  std::vector<std::string> errors;

  void add_libraries_directory(const std::string& value);

  void locate_and_add_library(const std::string& namespec,
                              const std::vector<boost::filesystem::path>& default_library_directories);
};

Dependencies parse_dependencies(const CompilationCommand& cmd,
                                const std::vector<boost::filesystem::path>& default_library_directories);

std::string redirect_gcc_output(const CompilationCommand& command, const std::string& to = {});

std::string redirect_ar_output(const CompilationCommand& command, const std::string& to = {});

#endif // COMMANDUTILS_HXX
