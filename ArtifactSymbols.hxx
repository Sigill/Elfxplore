#ifndef ARTIFACTS_SYMBOL_HXX
#define ARTIFACTS_SYMBOL_HXX

#include "SymbolReferenceSet.hxx"

struct ArtifactSymbols {
  SymbolReferenceSet undefined, external, internal;
};

#endif /* ARTIFACTS_SYMBOL_HXX */
