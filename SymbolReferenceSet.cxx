#include "SymbolReferenceSet.hxx"

void substract_set(SymbolReferenceSet& to, const SymbolReferenceSet& from) {
  for(const SymbolReference& s : from)
    to.erase(s);
}
