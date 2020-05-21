#ifndef PROGRESSBAR_HXX
#define PROGRESSBAR_HXX

#include <chrono>

class ProgressBar
{
private:
  size_t mExpectedCount, mCount;
  std::chrono::system_clock::time_point mStart, mNextUpdate;
  bool mEnabled;

public:
  ProgressBar();

  void start(const size_t expected_count);

  void operator++();
};

#endif // PROGRESSBAR_HXX
