#include "nm.hxx"

#include <regex>
#include <boost/process.hpp>

namespace bp = boost::process;

SymbolSet nm(const std::string& file, const std::string& options) {
  SymbolSet symbols;

  const std::regex symbol_regex("^.{16}(?: (.{16}))? (.) (.*$)");

  const std::string cmd = "nm " + options + " \"" + file + "\"";

  bp::ipstream pipe_stream;

  bp::child c(cmd, bp::std_out > pipe_stream);

  std::string line;
  std::smatch nm_match;
  while (pipe_stream && std::getline(pipe_stream, line) && !line.empty()) {
    if (std::regex_match(line, nm_match, symbol_regex)) {

      long long sz = (nm_match[1].length() == 0) ? -1 : std::stoll(nm_match[1], nullptr, 16);
      symbols.emplace(nm_match[3], nm_match[2], sz);
    }
  }

  c.wait();

  return symbols;
}

SymbolSet nm_undefined(const std::string& file) {
  return nm(file, "--undefined-only");
}

SymbolSet nm_defined(const std::string& file) {
  return nm(file, "-S --defined-only");
}

SymbolSet nm_defined_extern(const std::string& file) {
  return nm(file, "-S --defined-only --extern-only");
}
