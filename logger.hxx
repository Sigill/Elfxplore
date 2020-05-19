#ifndef LOGGER_HXX
#define LOGGER_HXX

#include <iostream>
#include <utility>

namespace logger {

enum severity_level
{
  trace,
  debug,
  info,
  warning,
  error,
  fatal,
  always
};

extern severity_level _severity_level;

struct EndlStream {
  explicit EndlStream(std::ostream& os = std::cout) : os(os) {}
  struct EndlStream_ {
    explicit EndlStream_(std::ostream& r) : os(r), live(true) {}
    EndlStream_(EndlStream_& r) : os(r.os), live(true) { r.live = false; }
    EndlStream_(EndlStream_&& r) : os(r.os), live(true) { r.live = false; }
    ~EndlStream_() { if(live) { os << '\n'; } }
    std::ostream& os;
    bool live;
  };
  std::ostream& os;
};

template <class T>
inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream::EndlStream_&& a, const T& t) {
  a.os << t;
  return std::move(a);
}

//template<class T>
//inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream& m, const T& t) {
//  return ::logger::EndlStream::EndlStream_(m.os) << t;
//}

template<class T>
inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream&& m, const T& t) {
  return ::logger::EndlStream::EndlStream_(m.os) << t;
}


inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream::EndlStream_&& a, std::ostream&(*pf)(std::ostream&)) {
  a.os << pf;
  return std::move(a);
}

//inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream& m, std::ostream&(*pf)(std::ostream&)) {
//  m.os << pf;
//  return ::logger::EndlStream::EndlStream_(m.os);
//}

inline ::logger::EndlStream::EndlStream_ operator<<(::logger::EndlStream&& m, std::ostream&(*pf)(std::ostream&)) {
  m.os << pf;
  return ::logger::EndlStream::EndlStream_(m.os);
}

inline bool log_enabled(const ::logger::severity_level lvl) {
  return ::logger::_severity_level <= lvl;
}

} // namespace logger

#define LOG_ENABLED(lvl) ::logger::log_enabled(::logger::severity_level::lvl)

#define SLOGGER std::cerr

#define LOGGER ::logger::EndlStream(SLOGGER)

#define LOG(lvl) \
  for(bool live = ::logger::_severity_level <= ::logger::lvl; live; live = false) \
    LOGGER

#define LOG2(cond, lvl1, lvl2) \
  for(bool live = ::logger::_severity_level <= ((cond) ? ::logger::lvl1 : ::logger::lvl2); live; live = false) \
    LOGGER

#endif // LOGGER_HXX
