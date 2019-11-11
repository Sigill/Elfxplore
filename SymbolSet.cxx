#include "SymbolSet.hxx"

void substract_set(SymbolSet& to, const SymbolSet& from) {
  for(const Symbol& s : from)
    to.erase(s);
}
