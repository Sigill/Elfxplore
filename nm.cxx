#include "nm.hxx"


#include <future>
#include <sstream>
#include <string.h>
#include <thread>

#include <boost/process.hpp>

#include "utils.hxx"

namespace bp = boost::process;

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options) {
//  const std::regex symbol_regex("^.{16}(?: (.{16}))? (.) (.*$)");

  ProcessResult process;
  process.command = "nm " + options + " \"" + file + "\"";

  bp::ipstream out_stream, err_stream;

  bp::child c(process.command,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::thread out_thread([&out_stream, &symbols](){
    std::string line;
//  std::smatch nm_match;
    while (out_stream && std::getline(out_stream, line) && !line.empty()) {
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

//    if (std::regex_match(line, nm_match, symbol_regex)) {
//      std::string name = nm_match[3];

//      if (name[0] == '.' // There's a lot of .LC??
//          || name.rfind('.') != std::string::npos)
//      continue;

//      long long sz = (nm_match[1].length() == 0) ? -1 : std::stoll(nm_match[1], nullptr, 16);
//      symbols.emplace(std::move(name), nm_match[2], sz);
//    }
    }
  });

  std::future<std::string> err_f = std::async(std::launch::async, [&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });

  process.err = err_f.get();
  rtrim(process.err);
  out_thread.join();

  c.wait();

  process.code = c.exit_code();

  return process;
}

//std::vector<ProcessResult> nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options)
//{
//  std::vector<ProcessResult> processes;

//  processes.emplace_back(nm_once(file, symbols, options));

//  if (symbols.empty())
//    processes.emplace_back(nm_once(file, symbols, options + " -D"));

//  return processes;
//}

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
