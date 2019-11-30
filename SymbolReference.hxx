#ifndef SYMBOL_REFERENCE_HXX
#define SYMBOL_REFERENCE_HXX

#include <string>
#include <set>

class SymbolReference
{
public:
  SymbolReference(std::string name, char type, long long address, long long size)
    : name(std::move(name)), type(type), address(address), size(size)
  {}

  std::string name;
  char type;
  long long address, size;
};

struct SymbolReferenceCmp {
  bool operator()(const SymbolReference& lhs, const SymbolReference& rhs) const;
};

#endif /* SYMBOL_REFERENCE_HXX */
