#ifndef PROCESSUTILS_HXX
#define PROCESSUTILS_HXX

#include <string>

struct ProcessResult {
  std::string command;
  std::string out, err;
  char code = -1;
};

#endif // PROCESSUTILS_HXX
