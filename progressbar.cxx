#include "progressbar.hxx"

#include <iostream>
#include <iomanip>
#include <unistd.h>

ProgressBar::ProgressBar()
  : mExpectedCount(0UL)
  , mCount(0UL)
{

}

void ProgressBar::start(const size_t expected_count) {
  mExpectedCount = expected_count;
  mStart = std::chrono::high_resolution_clock::now();
}

void ProgressBar::operator++()
{
  if (::isatty(fileno(stderr))) {
    ++mCount;
    const auto now = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration<double>(now - mStart).count();
    const double eta = mExpectedCount * elapsed / mCount - elapsed;
    std::cerr << mCount << "/" << mExpectedCount
              << " Elapsed: " << std::setw(4) << std::right << (int)elapsed << " s / "
              << "ETA: " << std::setw(4) << std::right << (int)eta << " s\r" << std::flush;
    if (mCount == mExpectedCount)
      std::cerr << std::endl;
  }
}
