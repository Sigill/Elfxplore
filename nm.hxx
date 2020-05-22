#ifndef NM_HXX
#define NM_HXX

#include <string>
#include <vector>
#include <iosfwd>
#include <functional>
#include <future>

#include "SymbolReferenceSet.hxx"
#include "process-utils.hxx"

bool failed(const ProcessResult& process);

void parse_nm_output(std::istream& stream, SymbolReferenceSet& symbols);

std::string read_stream(std::istream& stream);

ProcessResult nm(const std::string& file,
                 SymbolReferenceSet& symbols,
                 const std::string& options,
                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_undefined(const std::string& file,
                           SymbolReferenceSet& symbols,
                           const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                           const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_undefined_dynamic(const std::string& file,
                                   SymbolReferenceSet& symbols,
                                   const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                                   const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_defined(const std::string& file,
                         SymbolReferenceSet& symbols,
                         const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                         const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_defined_dynamic(const std::string& file,
                                 SymbolReferenceSet& symbols,
                                 const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                                 const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_defined_extern(const std::string& file,
                                SymbolReferenceSet& symbols,
                                const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                                const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm_defined_extern_dynamic(const std::string& file,
                                        SymbolReferenceSet& symbols,
                                        const std::function<std::future<void>(std::istream&, SymbolReferenceSet&)>& async_parse_out,
                                        const std::function<std::future<std::string>(std::istream&)>& async_parse_err);

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options);

ProcessResult nm_undefined(const std::string& file, SymbolReferenceSet& symbols);

ProcessResult nm_undefined_dynamic(const std::string& file, SymbolReferenceSet& symbols);

ProcessResult nm_defined(const std::string& file, SymbolReferenceSet& symbols);

ProcessResult nm_defined_dynamic(const std::string& file, SymbolReferenceSet& symbols);

ProcessResult nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols);

ProcessResult nm_defined_extern_dynamic(const std::string& file, SymbolReferenceSet& symbols);

#endif /* NM_HXX */
