#include "Symbol.hxx"

bool SymbolCmp::operator()(const Symbol& lhs, const Symbol& rhs) const {
  int c1 = lhs.name.compare(rhs.name);
  if (c1 < 0) {
    return true;
  } else if (c1 == 0) {
    int c2 = lhs.type.compare(rhs.type);
    if (c2 < 0) {
      return true;
    } else if (c2 == 0) {
      return lhs.size < rhs.size;
    } else {
      return false;
    }
  } else {
    return false;
  }
}
