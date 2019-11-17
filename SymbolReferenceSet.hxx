#ifndef SYMBOL_REFERENCE_SET_HXX
#define SYMBOL_REFERENCE_SET_HXX

#include "SymbolReference.hxx"

using SymbolReferenceSet = std::set<SymbolReference, SymbolReferenceCmp>;

void substract_set(SymbolReferenceSet& to, const SymbolReferenceSet& from);

#endif /* SYMBOL_REFERENCE_SET_HXX */
