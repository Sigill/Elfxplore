#ifndef DATABASEUTILS_HXX
#define DATABASEUTILS_HXX

#include <vector>
#include <string>
#include <functional>
#include <iosfwd>
#include <set>

#include "process-utils.hxx"
#include "ArtifactSymbols.hxx"
#include "ThreadPool.hpp"

class Database2;
class CompilationCommand;
class Artifact;

void import_command(Database2& db,
                    const std::string& line,
                    const std::function<void(const std::string&, const CompilationCommand&)>& notify);

void import_commands(Database2& db,
                     std::istream& in,
                     const std::function<void(const std::string&, const CompilationCommand&)>& notify);

void import_compile_commands(Database2& db,
                             std::istream& in,
                             const std::function<void(const std::string&, const CompilationCommand&)>& notify);

using DependenciesNotifier = void(const CompilationCommand&, const std::vector<Artifact>&, const std::vector<std::string>&);

class DependenciesExtractor {
public:
  std::function<void(const size_t)> notifyTotalSteps;
  std::function<DependenciesNotifier> notifyStep;
  void run(Database2& db);
};

struct SymbolExtractionStatus {
  std::vector<ProcessResult> processes;
  ArtifactSymbols symbols;
  bool linker_script = false;
};

bool has_failure(const std::vector<ProcessResult>& processes);

bool has_failure(const SymbolExtractionStatus& status);

class SymbolExtractor {
private:
  size_t pool_size;
  ThreadPool out_pool, err_pool;
  std::function<std::future<void>(std::istream& stream, SymbolReferenceSet& symbols)> out_runner;
  std::function<std::future<std::string>(std::istream& stream)> err_runner;

public:
  std::function<void(const size_t)> notifyTotalSteps;
  std::function<void(const Artifact&, const SymbolExtractionStatus&)> notifyStep;

  explicit SymbolExtractor(size_t pool_size);
  void run(Database2& db);
};

#endif // DATABASEUTILS_HXX
