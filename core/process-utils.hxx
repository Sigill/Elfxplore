#ifndef PROCESSUTILS_HXX
#define PROCESSUTILS_HXX

#include <string>

struct ProcessResult {
  std::string command;
  std::string out, err;
  char code = -1;
};

inline bool failed(const ProcessResult& process)
{
  return process.code != 0 || !process.err.empty();
}

#endif // PROCESSUTILS_HXX
