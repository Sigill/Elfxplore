#include "extract-task.hxx"

#include <iostream>
#include <algorithm>
#include <functional>
#include <fstream>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <termcolor/termcolor.hpp>

#include "ArtifactSymbols.hxx"
#include "Database2.hxx"
#include "utils.hxx"
#include "command-utils.hxx"
#include "nm.hxx"
#include "logger.hxx"
#include "database-utils.hxx"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;

namespace {

inline bool has_failure(const std::vector<ProcessResult>& processes) {
  return std::any_of(processes.begin(), processes.end(), failed);
}

struct SymbolExtractionStatus {
  std::vector<ProcessResult> processes;
  bool linker_script = false;
};

bool has_failure(const SymbolExtractionStatus& status) {
  return status.linker_script || has_failure(status.processes);
}

SymbolExtractionStatus extract_symbols_from_file(const std::string& usable_path, ArtifactSymbols& symbols) {
  SymbolExtractionStatus status;

  const std::string type = get_output_type(usable_path);
  const bool is_dynamic = type == "shared";

  std::ifstream file(usable_path, std::ios::in | std::ios::binary);
  char magic[4] = {0, 0, 0, 0};
  file.read(magic, 4);
  file.close();
  status.linker_script = !(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');

  if (!status.linker_script) {
    status.processes.emplace_back(nm_undefined(usable_path, symbols.undefined, symbol_table::normal));
    if (is_dynamic && symbols.undefined.empty())
      status.processes.emplace_back(nm_undefined(usable_path, symbols.undefined, symbol_table::dynamic));

    status.processes.emplace_back(nm_defined_extern(usable_path, symbols.external, symbol_table::normal));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm_defined_extern(usable_path, symbols.external, symbol_table::dynamic));

    status.processes.emplace_back(nm_defined(usable_path, symbols.external, symbol_table::normal));
    if (is_dynamic && symbols.external.empty())
      status.processes.emplace_back(nm_defined(usable_path, symbols.external, symbol_table::dynamic));

    substract_set(symbols.internal, symbols.external);
  }

  return status;
}

void insert_symbols(Database2& db, const std::string& path, ArtifactSymbols& symbols, const SymbolExtractionStatus& status) {
  long long artifact_id = db.artifact_id_by_name(path);

  if (artifact_id == -1) {
    db.create_artifact(path, get_output_type(path), false);
    artifact_id = db.last_id();
  }

  LOG(info || has_failure(status)) << termcolor::green << "Artifact #" << artifact_id << termcolor::reset << " " << path;

  LOG(always && status.linker_script) << termcolor::red << "Linker scripts are not supported" << termcolor::reset;

  for(const ProcessResult& process : status.processes) {
    LOG(always && failed(process)) << process.command;
    LOG(always && process.code != 0) << "Status: " << termcolor::red << (int)process.code << termcolor::reset;
    LOG(always && !process.err.empty()) << termcolor::red << "stderr: " << termcolor::reset << process.err;
  }

  db.insert_symbol_references(artifact_id, symbols);
}

class SymbolExtractor {
private:
  Database2& db;
  std::vector<ArtifactSymbols> symbols_pool;
  std::vector<SymbolExtractionStatus> extraction_status_pool;

public:
  explicit SymbolExtractor(Database2& db)
    : db(db), symbols_pool() , extraction_status_pool() {}

  void operator()(const std::vector<std::string>& files) {
    while(symbols_pool.size() < files.size()) {
      symbols_pool.emplace_back();
      extraction_status_pool.emplace_back();
    }

#pragma omp parallel for
    for(size_t i = 0; i < files.size(); ++i) {
      symbols_pool[i].undefined.clear();
      symbols_pool[i].external.clear();
      symbols_pool[i].internal.clear();
      extraction_status_pool[i].linker_script = false;
      extraction_status_pool[i].processes.clear();

      extraction_status_pool[i] = extract_symbols_from_file(files[i], symbols_pool[i]);
    }

    for(size_t i = 0; i < files.size(); ++i) {
      insert_symbols(db, files[i], symbols_pool[i], extraction_status_pool[i]);
    }
  }
};

template<typename T>
class BufferedTasks {
private:
  std::vector<T> buffer;
  size_t capacity;
  std::function<void(const std::vector<T>&)> process;

public:
  BufferedTasks(size_t N, std::function<void(const std::vector<T>&)> processor) : buffer(), capacity(N), process(processor) {
    buffer.reserve(N);
  }

  void processBuffer() {
    process(buffer);
    buffer.clear();
  }

  void push(const T& t) {
    buffer.push_back(t);
    if (buffer.size() == capacity)
      processBuffer();
  }

  void done() { processBuffer(); }
};

void log_dependencies(const CompilationCommand& cmd,
                      const std::vector<Artifact>& dependencies,
                      const std::vector<std::string>& errors) {
  LOG(info || !errors.empty()) << termcolor::green << "Command #" << cmd.id << termcolor::reset
                               << " " << cmd.directory << " " << cmd.executable << " " << cmd.args;

  for(const std::string& err : errors) {
    LOG(always) << termcolor::red << "Error: " << termcolor::reset << err;
  }

  LOG(debug) << termcolor::blue << ">" << termcolor::reset << " (" << cmd.output_type << ") " << cmd.artifact_id << " " << cmd.output;

  for (const Artifact& dependency : dependencies) {
    LOG(debug) << termcolor::yellow << "<" << termcolor::reset << " (" << dependency.type << ") " << dependency.id << " " << dependency.name;
  }
}

} // anonymous namespace

boost::program_options::options_description Extract_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("dependencies", "Extract dependencies from commands.")
      ("symbols", "Extract symbols from artifacts.")
      ;

  return opt;
}

int Extract_Task::execute(const std::vector<std::string>& args)
{
  bpo::variables_map vm;

  try {
    bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
    bpo::notify(vm);
  } catch(bpo::error &err) {
    std::cerr << err.what() << std::endl;
    return TaskStatus::ERROR;
  }

  if (vm.count("dependencies")) {
    extract_dependencies(db(), log_dependencies);

    LOGGER << termcolor::blue << "Status" << termcolor::reset;
    std::cout << db().count_artifacts() << " artifacts" << std::endl;
    for(const auto& type : db().count_artifacts_by_type()) {
      LOGGER << "\t" << type.second << " " << type.first;
    }
    std::cout << db().count_dependencies() << " dependencies" << std::endl;
  }

  if (vm.count("symbols")) {
    BufferedTasks<std::string> tasks(1, SymbolExtractor(db()));

    auto q = db().statement("select id, name, type from artifacts where type not in (\"source\", \"static\")");
    while (q.executeStep()) {
      std::string name = q.getColumn(1).getString();
      if (bfs::exists(name)) {
        tasks.push(name);
      }
    }

    tasks.done();

    LOGGER << termcolor::blue << "Status" << termcolor::reset;
    std::cout << db().count_symbols() << " symbols" << std::endl;
    std::cout << db().count_symbol_references() << " symbol references" << std::endl;
  }

  return 0;
}
