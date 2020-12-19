#ifndef PROGRESSBAR_HXX
#define PROGRESSBAR_HXX

#include <chrono>
#include <string>

class ProgressBar
{
private:
  std::string mMessage;
  size_t mExpectedCount, mCount;
  std::chrono::system_clock::time_point mStart, mNextUpdate;
  bool mEnabled;

public:
  ProgressBar(std::string message);

  void start(const size_t expected_count);

  void operator++();
};

#endif // PROGRESSBAR_HXX
