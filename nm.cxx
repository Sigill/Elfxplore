#include "nm.hxx"

#include <boost/process.hpp>
#include <string.h> // memrchr

namespace bp = boost::process;

void nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options) {
//  const std::regex symbol_regex("^.{16}(?: (.{16}))? (.) (.*$)");

  const std::string cmd = "nm " + options + " \"" + file + "\"";

  bp::ipstream pipe_stream;

  bp::child c(cmd, bp::std_out > pipe_stream);

  std::string line;
//  std::smatch nm_match;
  while (pipe_stream && std::getline(pipe_stream, line) && !line.empty()) {
    long long sz = -1;
    size_t offset = 17;

    if (line[offset] >= '0' && line[offset] <= '9') {
      const std::string size_str(line, 17, 16);
      sz = std::stoll(size_str, nullptr, 16);
      offset += 17;
    }

    // Filter-out:
    // .LC??
    // _GLOBAL__sub_I_*.cpp
    // symbol [clone .cold]
    // DW.ref.__gxx_personality_v0
    if (line[offset + 2] == '.' // There's a lot of .LC??
        || memrchr(&(line[offset + 2]), '.', line.length() - offset - 2) != NULL)
      continue;

    symbols.emplace(std::string(line, offset + 2), line[offset], sz);

//    if (std::regex_match(line, nm_match, symbol_regex)) {
//      std::string name = nm_match[3];

//      if (name[0] == '.' // There's a lot of .LC??
//          || name.rfind('.') != std::string::npos)
//      continue;

//      long long sz = (nm_match[1].length() == 0) ? -1 : std::stoll(nm_match[1], nullptr, 16);
//      symbols.emplace(std::move(name), nm_match[2], sz);
//    }
  }

  c.wait();
}

SymbolReferenceSet nm(const std::string& file, const std::string& options) {
  SymbolReferenceSet symbols;
  nm(file, symbols, options);
  return symbols;
}

void nm_undefined(const std::string& file, SymbolReferenceSet& symbols) {
  nm(file, symbols, "--undefined-only");
}

SymbolReferenceSet nm_undefined(const std::string& file) {
  return nm(file, "--undefined-only");
}

void nm_defined(const std::string& file, SymbolReferenceSet& symbols) {
  nm(file, symbols, "-S --defined-only");
}

SymbolReferenceSet nm_defined(const std::string& file) {
  return nm(file, "-S --defined-only");
}

void nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols) {
  nm(file, symbols, "-S --defined-only --extern-only");
}

SymbolReferenceSet nm_defined_extern(const std::string& file) {
  return nm(file, "-S --defined-only --extern-only");
}
