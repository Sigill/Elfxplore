#ifndef COMMANDUTILS_HXX
#define COMMANDUTILS_HXX

#include <string>
#include <vector>
#include <set>
#include <filesystem>

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

namespace parse_command_options {
  constexpr int with_directory = 1 << 0;
  constexpr int expand_path    = 1 << 1;
};

void parse_command(const std::string& line, CompilationCommand& command, const int options = parse_command_options::with_directory | parse_command_options::expand_path);

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
