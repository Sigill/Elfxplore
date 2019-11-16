#ifndef NM_HXX
#define NM_HXX

#include <string>

#include "Symbol.hxx"
#include "SymbolSet.hxx"

void nm(const std::string& file, SymbolSet& symbols, const std::string& options = std::string());

SymbolSet nm(const std::string& file, const std::string& options = std::string());

void nm_undefined(const std::string& file, SymbolSet& symbols);

SymbolSet nm_undefined(const std::string& file);

void nm_defined(const std::string& file, SymbolSet& symbols);

SymbolSet nm_defined(const std::string& file);

void nm_defined_extern(const std::string& file, SymbolSet& symbols);

SymbolSet nm_defined_extern(const std::string& file);

#endif /* NM_HXX */
