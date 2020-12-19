#include "nm.hxx"

#include <sstream>

#include <boost/process.hpp>

#include <instrmt/instrmt.hxx>

#include "utils.hxx"
#include "ThreadPool.hpp"

namespace bp = boost::process;

namespace {

std::future<void> async_symbol_parser(std::istream& stream, SymbolReferenceSet& symbols)
{
  return std::async(std::launch::async, [&stream, &symbols](){ parse_nm_output(stream, symbols); });
}

std::future<std::string> async_stream_reader(std::istream& stream)
{
  return std::async(std::launch::async, [&stream]() { return read_stream(stream); });
};

} // anonymous namespace

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const std::string& options,
                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err)
{
  INSTRMT_FUNCTION();

  ProcessResult process;
  process.command = "nm " + options + " \"" + file + "\"";

  bp::ipstream out_stream, err_stream;

  bp::child c(process.command,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::future<void> symbols_parsed = async_parse_out(out_stream, symbols);

  std::future<std::string> err_f = async_parse_err(err_stream);

  symbols_parsed.wait();
  process.err = err_f.get();
  rtrim(process.err);

  c.wait();

  process.code = c.exit_code();

  return process;
}

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const int flags,
                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err)
{
  std::string args;

  if (flags & nm_options::undefined)
    args = "--undefined-only";
  else if (flags & nm_options::defined)
    args = "-S --defined-only";
  else if (flags & nm_options::defined_extern)
    args = "-S --defined-only --extern-only";

  if (flags & nm_options::dynamic)
    args += " -D";

  return nm(file, symbols, args, async_parse_out, async_parse_err);
}

void parse_nm_output(std::istream& stream, SymbolReferenceSet& symbols)
{
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

std::string read_stream(std::istream& stream)
{
  std::ostringstream ss;
  ss << stream.rdbuf();
  return ss.str();
}

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const int flags)
{
  return nm(file, symbols, flags, async_symbol_parser, async_stream_reader);
}
