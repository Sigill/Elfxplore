#ifndef NM_HXX
#define NM_HXX

#include <string>

#include "Symbol.hxx"
#include "SymbolSet.hxx"

SymbolSet nm(const std::string& file, const std::string& options = std::string());

SymbolSet nm_undefined(const std::string& file);

SymbolSet nm_defined(const std::string& file);

SymbolSet nm_defined_extern(const std::string& file);

#endif /* NM_HXX */
