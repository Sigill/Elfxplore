#ifndef NM_HXX
#define NM_HXX

#include <string>

#include "SymbolReference.hxx"
#include "SymbolReferenceSet.hxx"

void nm(const std::string& file, SymbolReferenceSet& symbols, const std::string& options = std::string());

SymbolReferenceSet nm(const std::string& file, const std::string& options = std::string());

void nm_undefined(const std::string& file, SymbolReferenceSet& symbols);

SymbolReferenceSet nm_undefined(const std::string& file);

void nm_defined(const std::string& file, SymbolReferenceSet& symbols);

SymbolReferenceSet nm_defined(const std::string& file);

void nm_defined_extern(const std::string& file, SymbolReferenceSet& symbols);

SymbolReferenceSet nm_defined_extern(const std::string& file);

#endif /* NM_HXX */
