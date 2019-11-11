#ifndef SYMBOLSET_HXX
#define SYMBOLSET_HXX

#include "Symbol.hxx"

using SymbolSet = std::set<Symbol, SymbolCmp>;

void substract_set(SymbolSet& to, const SymbolSet& from);

#endif /* SYMBOLSET_HXX */
