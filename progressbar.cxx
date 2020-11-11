#include "progressbar.hxx"

#include <iostream>
#include <iomanip>
#include <unistd.h>

#include "logger.hxx"

using namespace std::chrono_literals;

ProgressBar::ProgressBar(std::string message)
  : mMessage(std::move(message))
  , mExpectedCount(0UL)
  , mCount(0UL)
  , mEnabled(::isatty(fileno(stderr)))
{
  if (mEnabled)
    LOG_FLUSH();
}

void ProgressBar::start(const size_t expected_count) {
  if (!mEnabled)
    return;

  mExpectedCount = expected_count;
  mStart = std::chrono::high_resolution_clock::now();
  mNextUpdate = mStart + 15ms;
}

void ProgressBar::operator++()
{
  if (!mEnabled)
    return;

  ++mCount;

  const auto now = std::chrono::high_resolution_clock::now();
  const bool last = mCount == mExpectedCount;
  if (now >= mNextUpdate || last)
  {
    mNextUpdate = now + 15ms;

    const double elapsed = std::chrono::duration<double>(now - mStart).count();
    const double eta = mExpectedCount * elapsed / mCount - elapsed;
    std::cerr << mMessage << ' ' << mCount << "/" << mExpectedCount
              << " Elapsed: " << std::setw(4) << std::right << (int)elapsed << " s / "
              << "ETA: " << std::setw(4) << std::right << (int)eta << " s\r" << std::flush;
    if (last)
      std::cerr << std::endl;
  }
}
