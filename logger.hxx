#ifndef LOGGER_HXX
#define LOGGER_HXX

#include <iostream>

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
    ~EndlStream_() { if(live) { os << std::endl; } }
    std::ostream& os;
    bool live;
  };
  std::ostream& os;
};

} // namespace logger

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


#define LOG(lvl) \
  for(bool live = ::logger::_severity_level <= ::logger::lvl; live; live = false) \
    ::logger::EndlStream(std::cout)

#define LOG_(lvl) \
  if (::logger::_severity_level > ::logger::lvl) {} \
  else ::logger::EndlStream(std::cout)

#define LOG2(cond, lvl1, lvl2) \
  for(bool live = ::logger::_severity_level <= ((cond) ? ::logger::lvl1 : ::logger::lvl2); live; live = false) \
    ::logger::EndlStream(std::cout)

#define LOG2_(cond, lvl1, lvl2) \
  if (::logger::_severity_level > (cond ? ::logger::lvl1 : ::logger::lvl2)) {} \
  else ::logger::EndlStream(std::cout)

#endif // LOGGER_HXX
