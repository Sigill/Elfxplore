#ifndef NM_HXX
#define NM_HXX

#include <string>
#include <vector>

#include "SymbolReferenceSet.hxx"
#include "process-utils.hxx"

bool failed(const ProcessResult& process);

enum class symbol_table {
  normal,
  dynamic
};

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options = std::string());

ProcessResult nm_undefined(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st);

ProcessResult nm_defined(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st);

ProcessResult nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols, const symbol_table st);

class ThreadPool;

ProcessResult nm(const std::string& file, SymbolReferenceSet& symbols,
                 ThreadPool& out_pool, ThreadPool& err_pool, const std::string& options = std::string());

ProcessResult nm_undefined(const std::string& file, SymbolReferenceSet& symbols,
                           ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st);

ProcessResult nm_defined(const std::string& file, SymbolReferenceSet& symbols,
                         ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st);

ProcessResult nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols,
                                ThreadPool& out_pool, ThreadPool& err_pool, const symbol_table st);

#endif /* NM_HXX */
