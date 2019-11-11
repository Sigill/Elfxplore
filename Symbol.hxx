#ifndef SYMBOL_HXX
#define SYMBOL_HXX

#include <string>
#include <set>

class Symbol
{
public:
  Symbol(std::string name, std::string type, long long size)
    : name(std::move(name)), type(std::move(type)), size(size)
  {}

  std::string name, type;
  long long size;
};

struct SymbolCmp {
  bool operator()(const Symbol& lhs, const Symbol& rhs) const;
};

#endif /* SYMBOL_HXX */
