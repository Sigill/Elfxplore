#include "SymbolReference.hxx"

bool SymbolReferenceCmp::operator()(const SymbolReference& lhs, const SymbolReference& rhs) const {
  int c1 = lhs.name.compare(rhs.name);
  if (c1 < 0) {
    return true;
  } else if (c1 == 0) {
    if (lhs.type < rhs.type) {
      return true;
    } else if (lhs.type == rhs.type) {
      if (lhs.address < rhs.address) {
        return true;
      } else if (lhs.address == rhs.address) {
        return lhs.size < rhs.size;
      } else {
        return false;
      }
    } else {
      return false;
    }
  } else {
    return false;
  }
}
