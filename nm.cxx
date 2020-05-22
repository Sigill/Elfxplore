#include "nm.hxx"


#include <future>
#include <sstream>
#include <string.h>
#include <thread>

#include <boost/process.hpp>

#include "utils.hxx"
#include "instrumentation.hxx"
#include "ThreadPool.h"

namespace bp = boost::process;

ITT_DOMAIN("elfxplore");

namespace {

void parse_nm_output(std::istream& stream, SymbolReferenceSet& symbols) {
  std::string line;
//  std::smatch nm_match;
  while (stream && std::getline(stream, line) && !line.empty()) {
    long long address = -1;
    long long sz = 0;
    size_t offset = 17;

    if (line[offset] >= '0' && line[offset] <= '9') {
      const std::string addr_str(line, 0, 16);
      address = std::stoll(addr_str, nullptr, 16);

      const std::string size_str(line, 17, 16);
      sz = std::stoll(size_str, nullptr, 16);
      offset = 34;
    }

    // Filter-out:
    // .LC??
    // _GLOBAL__sub_I_*.cpp
    // symbol [clone .cold]
    // DW.ref.__gxx_personality_v0
    if (line[offset + 2] == '.' // There's a lot of .LC??
        || memrchr(&(line[offset + 2]), '.', line.length() - offset - 2) != NULL
        || strncmp(&(line[offset + 2]), "__gmon_start__", sizeof("__gmon_start__")) == 0
        || strncmp(&(line[offset + 2]), "_ITM_", sizeof("_ITM_")) == 0)
      continue;

    symbols.emplace(std::string(line, offset + 2), line[offset], address, sz);
  }
}

} // anonymous namespace

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options) {
  ITT_FUNCTION_TASK();

  ProcessResult process;
  process.command = "nm " + options + " \"" + file + "\"";

  bp::ipstream out_stream, err_stream;

  bp::child c(process.command,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::future<void> symbols_parsed = std::async(std::launch::async, [&out_stream, &symbols](){ parse_nm_output(out_stream, symbols); });

  std::future<std::string> err_f = std::async(std::launch::async, [&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });

  symbols_parsed.wait();
  process.err = err_f.get();
  rtrim(process.err);

  c.wait();

  process.code = c.exit_code();

  return process;
}

ProcessResult  nm_undefined(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, "--undefined-only");
  else
    return nm(file, symbols, "--undefined-only -D");
}

ProcessResult nm_defined(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, "-S --defined-only");
  else
    return nm(file, symbols, "-S --defined-only -D");
}

ProcessResult nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, "-S --defined-only --extern-only");
  else
    return nm(file, symbols, "-S --defined-only --extern-only -D");
}

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols,
                 ThreadPool& out_pool, ThreadPool& err_pool, const std::string& options) {
  ITT_FUNCTION_TASK();

  ProcessResult process;
  process.command = "nm " + options + " \"" + file + "\"";

  bp::ipstream out_stream, err_stream;

  bp::child c(process.command,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::future<void> symbols_parsed = out_pool.enqueue(parse_nm_output, std::ref(out_stream), std::ref(symbols));

  std::future<std::string> err_f = err_pool.enqueue([&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });

  symbols_parsed.wait();
  process.err = err_f.get();
  rtrim(process.err);

  c.wait();

  process.code = c.exit_code();

  return process;
}

ProcessResult nm_undefined(const std::string& file, SymbolReferenceSet& symbols,
                           ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, out_pool, err_pool, "--undefined-only");
  else
    return nm(file, symbols, out_pool, err_pool, "--undefined-only -D");
}

ProcessResult nm_defined(const std::string& file, SymbolReferenceSet& symbols,
                         ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, out_pool, err_pool, "-S --defined-only");
  else
    return nm(file, symbols, out_pool, err_pool, "-S --defined-only -D");
}

ProcessResult nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols,
                                ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st) {
  if (st == symbol_table::normal)
    return nm(file, symbols, out_pool, err_pool, "-S --defined-only --extern-only");
  else
    return nm(file, symbols, out_pool, err_pool, "-S --defined-only --extern-only -D");
}
