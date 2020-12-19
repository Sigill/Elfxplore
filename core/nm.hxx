#ifndef NM_HXX
#define NM_HXX

#include <string>
#include <iosfwd>
#include <functional>
#include <future>

#include "SymbolReferenceSet.hxx"
#include "process-utils.hxx"

void parse_nm_output(std::istream& stream, SymbolReferenceSet& symbols);

std::string read_stream(std::istream& stream);

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const std::string& options,
                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

namespace nm_options {
  constexpr int dynamic                = 1 << 0;
  constexpr int undefined              = 1 << 1;
  constexpr int defined                = 1 << 2;
  constexpr int defined_extern         = 1 << 3;
  constexpr int undefined_dynamic      = dynamic | undefined;
  constexpr int defined_dynamic        = dynamic | defined;
  constexpr int defined_extern_dynamic = dynamic | defined_extern;
};

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const int flags,
                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const int flags);

#endif /* NM_HXX */
