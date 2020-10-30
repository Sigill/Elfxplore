#include "analyse-task.hxx"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <future>
#include <map>
#include <filesystem>

#include <boost/process.hpp>
#include <boost/asio.hpp>

#include <termcolor/termcolor.hpp>

#include "infix_iterator.hxx"

#include "Database3.hxx"
#include "query-utils.hxx"
#include "utils.hxx"
#include "command-utils.hxx"
#include "logger.hxx"
#include "progressbar.hxx"
#include "csvprinter.h"
#include "infix_iterator.hxx"
#include "process-utils.hxx"
#include "utils.hxx"

#include "linemarkers/linemarkers.hxx"

namespace bpo = boost::program_options;
namespace fs = std::filesystem;
namespace bp = boost::process;

namespace {

enum class useless_dependencies_analysis_modes {
  symbols,
  ldd
};

void validate(boost::any& v,
              const std::vector<std::string>& values,
              useless_dependencies_analysis_modes* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  const std::string& s = bpo::validators::get_single_string(values);

  if (s == "symbols")
    v = useless_dependencies_analysis_modes::symbols;
  else if (s == "ldd")
    v = useless_dependencies_analysis_modes::ldd;
  else
    throw bpo::invalid_option_value(s);
}

enum class command_analysis_mode {
  source_count,
  preprocessor_count,
  preprocessor_time,
  compile_time,
  link_time,
  all
};

void validate(boost::any& v,
              const std::vector<std::string>& values,
              command_analysis_mode* /*target_type*/, int)
{
  // Make sure no previous assignment to 'v' was made.
  bpo::validators::check_first_occurrence(v);

  const std::string& s = bpo::validators::get_single_string(values);

  if (s == "source-count")
    v = command_analysis_mode::source_count;
  else if (s == "preprocessor-count")
    v = command_analysis_mode::preprocessor_count;
  else if (s == "preprocessor-time")
    v = command_analysis_mode::preprocessor_time;
  else if (s == "compile-time")
    v = command_analysis_mode::compile_time;
  else if (s == "link-time")
    v = command_analysis_mode::link_time;
  else if (s == "all")
    v = command_analysis_mode::all;
  else
    throw bpo::invalid_option_value(s);
}

std::vector<command_analysis_mode> expand_modes(const std::vector<command_analysis_mode>& in) {
  std::vector<command_analysis_mode> out;
  for(const command_analysis_mode mode : in) {
    if (mode == command_analysis_mode::all) {
      out.insert(out.end(), {command_analysis_mode::source_count,
                             command_analysis_mode::preprocessor_count,
                             command_analysis_mode::preprocessor_time,
                             command_analysis_mode::compile_time,
                             command_analysis_mode::link_time});
    } else {
      out.push_back(mode);
    }
  }

  std::sort(out.begin(), out.end());

  return out;
}

void analyse_duplicated_symbols(Database2& db,
                                const std::vector<std::string>& included_types,
                                const std::vector<std::string>& excluded_types,
                                const std::vector<std::string>& included_categories,
                                const std::vector<std::string>& excluded_categories)
{
  std::vector<std::string> conditions;

  auto build_condition = [&conditions] (const char* expr, const std::vector<std::string>& values) {
    if (!values.empty()) {
      std::stringstream ss;
      ss << expr << " " << in_expr(values);
      conditions.emplace_back(ss.str());
    }
  };

  build_condition("artifacts.type in", included_types);
  build_condition("artifacts.type not in", excluded_types);
  build_condition("symbol_references.category in", included_categories);
  build_condition("symbol_references.category not in", excluded_categories);

  std::stringstream duplicated_symbols_query;
  duplicated_symbols_query << R"(
select symbols.id, symbols.name as name, symbols.dname as dname, count(symbol_references.id) as occurences, sum(symbol_references.size) as total_size
from symbols
inner join symbol_references on symbols.id = symbol_references.symbol_id
inner join artifacts on artifacts.id = symbol_references.artifact_id
where symbol_references.size > 0
)";

  if (!conditions.empty()) {
    duplicated_symbols_query << "and ";
    std::copy(conditions.cbegin(), conditions.cend(), infix_ostream_iterator<std::string>(duplicated_symbols_query, "\nand "));
  }

  duplicated_symbols_query << R"(
group by symbols.id
having occurences > 1
order by total_size desc, name asc;
)";

  std::cout << duplicated_symbols_query.str() << std::endl;

  SQLite::Statement duplicated_symbols_stm = db.statement(duplicated_symbols_query.str());

  while (duplicated_symbols_stm.executeStep()) {
    std::cout << symbol_hname(duplicated_symbols_stm.getColumn(1).getString(), duplicated_symbols_stm.getColumn(2).getString())
              << ": occurences: " << duplicated_symbols_stm.getColumn(3).getInt64()
              << ", total size: " << duplicated_symbols_stm.getColumn(4).getInt64() << std::endl;
  }
}

template<typename K, typename V, typename C, typename A>
std::vector<K> map_keys(const std::map<K, V, C, A>& map) {
  std::vector<K> v(map.size());
  std::transform(map.begin(), map.end(), v.begin(),
                 [](const std::pair<K, V>& pair) -> K { return pair.first; });
  return v;
}

std::set<long long> find_unresolved_symbols(Database2& db, const long long artifact_id)
{
  const std::vector<long long> undefined_symbols = db.undefined_symbols(artifact_id);
  std::set<long long> unresolved_symbols(undefined_symbols.cbegin(), undefined_symbols.cend());

  std::stringstream ss;
  ss << R"(
select symbol_id
from symbol_references
inner join dependencies on symbol_references.artifact_id = dependencies.dependency_id
where symbol_references.category = "external"
and dependencies.dependee_id = ?
and symbol_references.symbol_id in )" << in_expr(undefined_symbols);

  SQLite::Statement stm = db.statement(ss.str());
  stm.bind(1, artifact_id);

  while(stm.executeStep()) {
    unresolved_symbols.erase(stm.getColumn(0).getInt64());
  }

  return unresolved_symbols;
}

template<typename T>
std::vector<T> as_vector(const std::set<T>& s)
{
  return std::vector<T>(s.cbegin(), s.cend());
}

void analyse_undefined_symbols(Database2& db, const std::vector<long long>& artifacts)
{
  for(const long long artifact_id : artifacts) {
    const std::vector<long long> undefined_symbols = as_vector(find_unresolved_symbols(db, artifact_id));

    if (!undefined_symbols.empty()) {
      const std::map<long long, std::vector<std::string>> resolving_artifacts = db.resolve_symbols(undefined_symbols);

      std::cout << db.artifact_name_by_id(artifact_id) << "\n";

      for(const std::pair<long long, std::string>& undefined_symbol : get_symbol_hnames(db, undefined_symbols)) {
        std::cout << "\t" << undefined_symbol.second;

        auto where_resolved = resolving_artifacts.find(undefined_symbol.first);
        if (where_resolved != resolving_artifacts.cend()) {
          std::cout << " -> ";
          std::copy(where_resolved->second.cbegin(), where_resolved->second.cend(), infix_ostream_iterator<std::string>(std::cout, ", "));
        }
        std::cout << "\n";
      }
    }
  }
}

std::vector<std::string> get_useless_dependencies(Database2& db,
                                                  const long long dependee_id,
                                                  const std::vector<long long>& useful_dependencies)
{
  std::stringstream useless_dependencies_q;
  useless_dependencies_q << R"(
select artifacts.name
from artifacts
inner join dependencies on dependencies.dependency_id = artifacts.id
where artifacts.type = "shared"
and dependencies.dependee_id = ?
and dependencies.dependency_id not in )" << in_expr(useful_dependencies);

  SQLite::Statement useless_dependencies_stm = db.statement(useless_dependencies_q.str());
  useless_dependencies_stm.bind(1, dependee_id);

  std::vector<std::string> useless_dependencies;

  while(useless_dependencies_stm.executeStep()) {
    useless_dependencies.emplace_back(useless_dependencies_stm.getColumn(0).getString());
  }

  return useless_dependencies;
}

std::vector<long long> get_shared_dependencies(Database2& db, const long long dependee_id)
{
  SQLite::Statement dependencies_stm = db.build_get_depend_stm("dependency_id", "dependee_id", {"shared"}, {});
  dependencies_stm.bind(1, dependee_id);
  return Database2::get_ids(dependencies_stm);
}

std::vector<long long> get_useful_dependencies_simple1(Database2& db, const long long dependee_id)
{
  std::stringstream useful_dependencies_q;
  useful_dependencies_q << R"(
select distinct symbol_references.artifact_id
from symbol_references
where symbol_references.artifact_id in)" << in_expr(get_shared_dependencies(db, dependee_id)) << R"(
and symbol_references.category = "external"
and symbol_references.symbol_id in )" << in_expr(db.undefined_symbols(dependee_id));

  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
  return Database2::get_ids(useful_dependencies_stm);
}

std::map<long long, std::vector<long long>> detail_useful_dependencies(Database2& db, const long long dependee_id)
{
  std::stringstream useful_dependencies_q;
  useful_dependencies_q << R"(
select symbol_references.artifact_id, symbol_references.symbol_id
from symbol_references
where symbol_references.artifact_id in)" << in_expr(get_shared_dependencies(db, dependee_id)) << R"(
and symbol_references.category = "external"
and symbol_references.symbol_id in )" << in_expr(db.undefined_symbols(dependee_id));

  std::map<long long, std::vector<long long>> resolved_symbols;

  SQLite::Statement stm = db.statement(useful_dependencies_q.str());
  while (stm.executeStep()) {
    resolved_symbols[stm.getColumn(0).getInt64()].push_back(stm.getColumn(1).getInt64());
  }

  return resolved_symbols;
}

//std::vector<long long> get_useful_dependencies_simple2(Database2& db, const long long dependee_id)
//{
//  const std::vector<long long> undefined_symbols = get_undefined_symbols(db, dependee_id);

//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct symbol_references.artifact_id
//from symbol_references
//inner join dependencies on dependencies.dependency_id = symbol_references.artifact_id
//inner join artifacts on artifacts.id = symbol_references.artifact_id
//where dependencies.dependee_id = ?
//and artifacts.type = "shared"
//and symbol_references.category = "external"
//and symbol_references.symbol_id in )" << in_expr(undefined_symbols);

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

//std::vector<long long> get_useful_dependencies_join(Database2& db, const long long dependee_id)
//{
//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct dependencies.dependency_id
//from dependencies
//inner join symbol_references as external_references on external_references.artifact_id = dependencies.dependency_id
//inner join symbol_references as undefined_references on undefined_references.artifact_id = dependencies.dependee_id
//                                                  and undefined_references.symbol_id = external_references.symbol_id
//inner join artifacts /*indexed by artifact_by_type*/ on artifacts.id = dependencies.dependency_id
//where external_references.category = "external"
//and undefined_references.category = "undefined"
//and artifacts.type = "shared"
//and dependencies.dependee_id = ?)";

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

//std::vector<long long> get_useful_dependencies_inner_query(Database2& db, const long long dependee_id)
//{
//  std::stringstream useful_dependencies_q;
//  useful_dependencies_q << R"(
//select distinct dependencies.dependee_id, dependencies.dependency_id
//from dependencies
//inner join artifacts /*indexed by artifact_by_type*/ on artifacts.id = dependencies.dependency_id
//where artifacts.type = "shared"
//and dependencies.dependee_id = ?
//and not exists (
//select 1
//from symbol_references as undefined_references
//inner join symbol_references as external_references on external_references.artifact_id = dependencies.dependency_id
//                                                 and undefined_references.symbol_id = external_references.symbol_id
//where undefined_references.artifact_id = dependencies.dependee_id
//and external_references.category = "external"
//and undefined_references.category = "undefined"
//)
//)";

//  SQLite::Statement useful_dependencies_stm = db.statement(useful_dependencies_q.str());
//  useful_dependencies_stm.bind(1, dependee_id);
//  return Database2::get_ids(useful_dependencies_stm);
//}

std::vector<std::string> get_useless_dependencies(Database2& db,
                                                  const long long dependee_id)
{
  const std::vector<long long> useful_dependencies = get_useful_dependencies_simple1(db, dependee_id);

  auto useless_dependencies = get_useless_dependencies(db, dependee_id, useful_dependencies);
  std::sort(useless_dependencies.begin(), useless_dependencies.end());
  return useless_dependencies;
}

std::vector<long long> get_generated_shared_libs_and_executables(Database2& db, const std::vector<std::string>& selection)
{
  std::vector<long long> artifacts;

  std::stringstream ss;
  ss << R"(
select id
from artifacts
where generating_command_id is not NULL
)";

  if (selection.empty()) {
    ss << R"(and artifacts.type in ("shared", "executable"))" << "\n";
  } else {
    ss << "and artifacts.name in " << in_expr(selection) << "\n";
  }

  SQLite::Statement stm = db.statement(ss.str());

  while(stm.executeStep()) {
    artifacts.emplace_back(stm.getColumn(0));
  }

  return artifacts;
}

void analyse_useless_dependencies_symbols(Database2& db, const std::vector<long long>& artifacts)
{
  for(const long long artifact_id : artifacts) {
    const std::vector<std::string> useless_dependencies = get_useless_dependencies(db, artifact_id);

    LOG(debug || !useless_dependencies.empty())
        << termcolor::green << "Artifact " << artifact_id << termcolor::reset << " " << db.artifact_name_by_id(artifact_id);

    if (LOG_ENABLED(debug)) {
      LOGGER << "Dynamic dependencies ";
      for(const long long dependency_id : get_shared_dependencies(db, artifact_id)) {
        LOGGER << "\t" << termcolor::blue << dependency_id << termcolor::reset << " " << db.artifact_name_by_id(dependency_id);
      }

      const std::map<long long, std::vector<long long>> resolved_symbols = detail_useful_dependencies(db, artifact_id);

      for(const auto& useful_dependency : resolved_symbols) {
        const long long dependency_id = useful_dependency.first;
        LOGGER << termcolor::green << "Artifact " << dependency_id << termcolor::reset << " " << db.artifact_name_by_id(dependency_id)
               << " resolves symbols: ";
        const std::map<long long, std::string> symbols = get_symbol_hnames(db, useful_dependency.second);
        for(const auto& symbol : symbols) {
          SLOGGER << "\t" << termcolor::blue << symbol.first << termcolor::reset << " " << symbol.second << std::endl;
        }
        SLOGGER << std::endl;
      }
    }

    if (!useless_dependencies.empty()) {
      LOG(debug) << termcolor::green << "Useless dependencies:" << termcolor::reset;

      for(const std::string& ud : useless_dependencies) {
        LOG(always) << "\t" << ud;
      }
    }
  }
}

const std::string& ldd() {
  static std::string path = []{
    bool found;
    std::string p = which("ldd", found);
    if (!found)
      throw std::runtime_error("Unable to locate \"ldd\" executable");
    return p;
  }();
  return path;
}

void analyse_useless_dependencies_ldd(Database2& db, const std::vector<long long>& artifacts)
{
  for(const long long artifact_id : artifacts) {
    const std::string artifact = db.artifact_name_by_id(artifact_id);
    boost::asio::io_service ios;

    std::future<std::string> out_, err_;

    bp::child c(ldd(), "-u", "-r", artifact,
                bp::std_in.close(),
                bp::std_out > out_,
                bp::std_err > err_,
                ios);

    ios.run();

    std::vector<std::string> useless_dependencies;

    if (c.exit_code() != 0) {
      std::stringstream ss(out_.get());
      std::string line;
      if (ss) std::getline(ss, line); // Skip first line

      while(ss && std::getline(ss, line)) {
        useless_dependencies.emplace_back(ltrim_copy(line));
      }
    }

    std::sort(useless_dependencies.begin(), useless_dependencies.end());

    const std::string err = trim_copy(err_.get());

    LOG(always && (!useless_dependencies.empty() || !err.empty())) << termcolor::green << "Artifact #" << artifact_id << termcolor::reset << " " << artifact;

    for(const std::string& ud : useless_dependencies) {
      LOG(always) << "\t" << ud;
    }

    LOG(warning) << termcolor::red << "stderr: " << termcolor::reset << err;
  }
}

class Measure_ {
public:
  virtual void print(csv::printer& csv) const = 0;
};

template <typename T>
class Measure : public Measure_ {
public:
  T value;
  Measure() : value() {}
  void print(csv::printer& csv) const override { csv << value; }
};

std::vector<CompilationCommand> get_object_commands(Database2& db)
{
  std::vector<CompilationCommand> commands;

  const char* q = R"(
select commands.id, commands.directory, commands.executable, commands.args, artifacts.name
from commands
inner join artifacts
on artifacts.generating_command_id = commands.id
where artifacts.type = "object"
)";

  SQLite::Statement stm = db.statement(q);

  while(stm.executeStep()) {
    CompilationCommand cmd;
    cmd.id         = stm.getColumn(0).getInt64();
    cmd.directory  = stm.getColumn(1).getString();
    cmd.executable = stm.getColumn(2).getString();
    cmd.args       = stm.getColumn(3).getString();
    cmd.output     = stm.getColumn(4).getString();

    commands.emplace_back(std::move(cmd));
  }

  return commands;
}

std::vector<CompilationCommand> get_link_commands(Database2& db)
{
  std::vector<CompilationCommand> commands;

  const char* q = R"(
select commands.id, commands.directory, commands.executable, commands.args, artifacts.name
from commands
inner join artifacts
on artifacts.generating_command_id = commands.id
where artifacts.type in ("static", "shared", "executable")
)";

  SQLite::Statement stm = db.statement(q);

  while(stm.executeStep()) {
    CompilationCommand cmd;
    cmd.id         = stm.getColumn(0).getInt64();
    cmd.directory  = stm.getColumn(1).getString();
    cmd.executable = stm.getColumn(2).getString();
    cmd.args       = stm.getColumn(3).getString();
    cmd.output     = stm.getColumn(4).getString();

    commands.emplace_back(std::move(cmd));
  }

  return commands;
}

ProcessResult time_command(const std::string& cmd, const std::string& directory, double& duration) {
  ProcessResult res;
  res.command = cmd;

  boost::asio::io_service ios;
  std::future<std::string> err_;

  const auto start = std::chrono::high_resolution_clock::now();

  bp::child p(cmd,
              bp::start_dir = directory,
              bp::std_in.close(),
              bp::std_out > bp::null,
              bp::std_err > err_,
              ios);

  ios.run();
  p.wait();

  const std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
  duration = elapsed.count();

  res.code = p.exit_code();
  res.err = err_.get();

  return res;
}

ProcessResult wc_preprocessor(const CompilationCommand& command, size_t& c, size_t& l) {
  ProcessResult res;
  res.command = redirect_gcc_output(command) + " -E";

  bp::ipstream out_stream, err_stream;

  bp::child p(res.command,
              bp::start_dir = command.directory,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::thread out_thread([&out_stream, &c, &l](){ wc(out_stream, c, l); });
  std::future<std::string> err_f = std::async(std::launch::async, [&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });
  out_thread.join();

  p.wait();

  res.code = p.exit_code();
  res.err = err_f.get();

  return res;
}

ProcessResult time_preprocessor(const CompilationCommand& command, double& duration) {
  const std::string cmd = redirect_gcc_output(command, "/dev/null") + " -E";
  return time_command(cmd, command.directory, duration);
}

ProcessResult time_compile(const CompilationCommand& command, double& duration) {
  const std::string cmd = redirect_gcc_output(command, "/dev/null");
  return time_command(cmd, command.directory, duration);
}

ProcessResult time_link(const CompilationCommand& command, double& duration) {
  if (is_cc(command.executable)) {
    const std::string cmd = redirect_gcc_output(command, "/dev/null");
    return time_command(cmd, command.directory, duration);
  } else {
    const FileSystemGuard a = FileSystemGuard(fs::temp_directory_path() / (random_alnum(16) + ".a"));
    const std::string cmd = redirect_ar_output(command, a.path().string());
    return time_command(cmd, command.directory, duration);
  }
}

using MeasuresMap = std::map<std::string, std::reference_wrapper<const Measure_>>;

void print(csv::printer& csv,
           const CompilationCommand& command,
           const std::vector<std::string>& inputs,
           const MeasuresMap& measures,
           const std::vector<std::string>& columns) {
  std::ostringstream ss;
  std::copy(inputs.cbegin(), inputs.cend(), infix_ostream_iterator<std::string>(ss, ";"));
  csv << ss.str();

  csv << command.output;

  for(const std::string& k : columns) {
    auto it = measures.find(k);
    if (it != measures.end()) { it->second.get().print(csv); }
    else { csv << csv::empty; }
  }

  csv << command.directory << (command.executable + " " + command.args);

  csv << csv::endrow;
}

void log_command_error(const std::string& directory, const ProcessResult& res) {
  LOG(always) << directory;
  LOG(always) << res.command;
  LOG(always) << (int)res.code;
  LOG(always) << res.err;
}

void analyse_commands(Database2& db, const std::vector<command_analysis_mode>& modes, const unsigned int num_threads, std::ostream& out) {
  const bool analyse_source = std::find(modes.begin(), modes.end(), command_analysis_mode::source_count) != modes.end();
  const bool analyse_preprocessor_count = std::find(modes.begin(), modes.end(), command_analysis_mode::preprocessor_count) != modes.end();
  const bool analyse_preprocessor_time = std::find(modes.begin(), modes.end(), command_analysis_mode::preprocessor_time) != modes.end();
  const bool analyse_compile_time = std::find(modes.begin(), modes.end(), command_analysis_mode::compile_time) != modes.end();
  const bool analyse_link_time = std::find(modes.begin(), modes.end(), command_analysis_mode::link_time) != modes.end();

  std::vector<std::string> columns;
  if (analyse_source)
    columns.insert(columns.end(), {"source-chars", "source-lines"});
  if (analyse_preprocessor_count)
    columns.insert(columns.end(), {"preprocessor-chars", "preprocessor-lines"});
  if (analyse_preprocessor_time)
    columns.emplace_back("preprocessor-time");
  if (analyse_compile_time || analyse_link_time)
    columns.emplace_back("command-time");

  csv::printer csv = csv::printer(out);
  csv << "inputs" << "output";
  for(const std::string& c : columns) csv << c;
  csv << "directory" << "command" << csv::endrow;

  if (analyse_source || analyse_preprocessor_count || analyse_preprocessor_time || analyse_compile_time) {
    LOG(always) << "Analysing compilation commands";

    auto commands = get_object_commands(db);

    ProgressBar progress;
    progress.start(commands.size());

#pragma omp parallel for num_threads(num_threads) schedule(guided)
    for(size_t i = 0; i < commands.size(); ++i) {
      const CompilationCommand& command = commands[i];

      Measure<size_t> source_chars, source_lines, preprocessor_chars, preprocessor_lines;
      Measure<double> preprocessor_time, compile_time;
      MeasuresMap measures;
      std::vector<std::string> inputs;

#pragma omp critical
      inputs = db.get_sources(command.id);

      if (analyse_source) {
        for(const std::string& source : inputs) {
          wc(source, source_chars.value, source_lines.value);
        }
        measures.emplace("source-chars", source_chars);
        measures.emplace("source-lines", source_lines);
      }

      if (analyse_preprocessor_count) {
        const ProcessResult res = wc_preprocessor(command, preprocessor_chars.value, preprocessor_lines.value);
        measures.emplace("preprocessor-chars", preprocessor_chars);
        measures.emplace("preprocessor-lines", preprocessor_lines);
        if (res.code != 0) {
#pragma omp critical
          log_command_error(command.directory, res);
        }
      }

      if (analyse_preprocessor_time) {
        const ProcessResult res = time_preprocessor(command, preprocessor_time.value);
        measures.emplace("preprocessor-time", preprocessor_time);
        if (res.code != 0) {
#pragma omp critical
          log_command_error(command.directory, res);
        }
      }

      if (analyse_compile_time) {
        const ProcessResult res = time_compile(command, compile_time.value);
        measures.emplace("command-time", compile_time);
        if (res.code != 0) {
#pragma omp critical
          log_command_error(command.directory, res);
        }
      }

#pragma omp critical
      {
        print(csv, command, inputs, measures, columns);
        ++progress;
      }
    }
  }

  if (analyse_link_time) {
    LOG(always) << "Analysing link commands";

    auto commands = get_link_commands(db);

    ProgressBar progress;
    progress.start(commands.size());

#pragma omp parallel for num_threads(num_threads) schedule(guided)
    for(size_t i = 0; i < commands.size(); ++i) {
      const CompilationCommand& command = commands[i];

      MeasuresMap measures;
      std::vector<std::string> inputs;

      Measure<double> link_time;
      const ProcessResult res = time_link(command, link_time.value);
      measures.emplace("command-time", link_time);

#pragma omp critical
      {
        if (res.code != 0)
          log_command_error(command.directory, res);

        const long long artifact_id = db.artifact_id_by_command(command.id);
        for(const long long dependency_id : db.dependencies(artifact_id)) {
          inputs.emplace_back(db.artifact_name_by_id(dependency_id));
        }
        print(csv, command, inputs, measures, columns);
        ++progress;
      }
    }
  }
}

ProcessResult list_includes(const CompilationCommand& command,
                            IncludeTree& include_tree) {
  ProcessResult res;
  res.command = redirect_gcc_output(command) + " -E";

  bp::ipstream out_stream, err_stream;

  bp::child p(res.command,
              bp::start_dir = command.directory,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::future<IncludeTree> include_tree_f = std::async(std::launch::async, [&out_stream](){
    return IncludeTree::from_stream(out_stream, false);
  });

  std::future<std::string> err_f = std::async(std::launch::async, [&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });

  p.wait();

  include_tree = include_tree_f.get();

  res.code = p.exit_code();
  res.err = err_f.get();

  return res;
}

void analyse_includes(Database2& db, const unsigned int num_threads) {
  auto commands = get_object_commands(db);

  ProgressBar progress;
  progress.start(commands.size());

#pragma omp parallel for num_threads(num_threads) schedule(guided)
  for(size_t i = 0; i < commands.size(); ++i) {

    IncludeTree include_tree;
    auto res = list_includes(commands[i], include_tree);

#pragma omp critical
    {
      if (res.code == 0) {
        LOG(always) << termcolor::green
                    << commands[i].directory << " "
                    << commands[i].executable << " "
                    << commands[i].args << termcolor::reset;

        preorder_walk(include_tree, [](const PreprocessedFile& file){
          LOGGER << io::repeat("| ", file.depth - 1)
                 << file.included_at_line << " "
                 << file.filename << " (" << file.lines_count << " / " << file.cumulated_lines_count << " lines)";
        });
      } else {
        log_command_error(commands[i].directory, res);
      }

      ++progress;
    }
  }
}

} // anonymous namespace

boost::program_options::options_description Analyse_Task::options()
{
  bpo::options_description opt("Options");
  opt.add_options()
      ("type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts matching those types.")
      ("not-type",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider artifacts not matching those types.")
      ("category",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider references matching those categories.")
      ("not-category",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Only consider references not matching those categories.")
      ("artifact",
       bpo::value<std::vector<std::string>>()->multitoken()->default_value({}, ""),
       "Artifact to export.")
      (",j",
       bpo::value<unsigned int>(&mNumThreads)->default_value(1),
       "Number of parallel threads ro run.")
      ("duplicated-symbols", "Analyse duplicated symbols.")
      ("undefined-symbols", "Analyse undefined symbols.")
      ("useless-dependencies",
       bpo::value<useless_dependencies_analysis_modes>()->implicit_value(useless_dependencies_analysis_modes::symbols, "symbols"),
       "Analyse useless dependencies.\n"
       "There are two analysis mode:\n"
       "- symbols: identify symbols not exported by dependencies.\n"
       "- ldd: equivalent to ldd -u -r.")
      ("command",
       bpo::value<std::vector<command_analysis_mode>>()->implicit_value({command_analysis_mode::all}, "all"),
       "Analyse compilation commands (source_count, preprocessor-count, preprocessor-time, compile-time, link-time, all).")
      ("includes", "Analyse include tree.")
      ;

  return opt;
}

void Analyse_Task::parse_args(const std::vector<std::string>& args)
{
  bpo::store(bpo::command_line_parser(args).options(options()).run(), vm);
  bpo::notify(vm);

  if (vm.count("duplicated-symbols")
      + vm.count("undefined-symbols")
      + vm.count("useless-dependencies")
      + vm.count("command")
      + vm.count("includes") != 1) {
    throw bpo::error("Invalid analysis type");
  }
}

int Analyse_Task::execute(Database3& db)
{
  if (vm.count("duplicated-symbols")) {
    db.load_symbols();

    analyse_duplicated_symbols(db,
                               vm["type"].as<std::vector<std::string>>(),
                               vm["not-type"].as<std::vector<std::string>>(),
                               vm["category"].as<std::vector<std::string>>(),
                               vm["not-category"].as<std::vector<std::string>>());
  } else if (vm.count("undefined-symbols")) {
    db.load_symbols();

    const std::vector<long long> artifacts = get_generated_shared_libs_and_executables(db, vm["artifact"].as<std::vector<std::string>>());
    analyse_undefined_symbols(db, artifacts);
  } else if (vm.count("useless-dependencies")) {
    db.load_dependencies();

    const auto mode = vm["useless-dependencies"].as<useless_dependencies_analysis_modes>();
    const std::vector<long long> artifacts = get_generated_shared_libs_and_executables(db, vm["artifact"].as<std::vector<std::string>>());

    if (mode == useless_dependencies_analysis_modes::symbols) {
      analyse_useless_dependencies_symbols(db, artifacts);
    } else if (mode == useless_dependencies_analysis_modes::ldd) {
      analyse_useless_dependencies_ldd(db, artifacts);
    }
  } else if (vm.count("command")) {
    const auto modes = expand_modes(vm["command"].as<std::vector<command_analysis_mode>>());

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::ostringstream ss;
    ss << fs::current_path().string() << "/" << std::put_time(std::localtime(&now), "elfxplore-commands-%Y-%m-%d-%H-%M-%S.csv");

    std::ofstream out(ss.str());
    analyse_commands(db, modes, mNumThreads, out);
    out.close();
  } else if (vm.count("includes")) {
    analyse_includes(db, mNumThreads);
  }

  return 0;
}
