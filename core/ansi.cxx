#include "ansi.hxx"

#include <unistd.h> // isatty(), fileno()

#include <iostream>
#include <cstdio>

namespace {
static int ainsi_support_index = std::ios_base::xalloc();

FILE* get_standard_stream(const std::ostream& stream)
{
  if (&stream == &std::cout)
    return stdout;
  else if ((&stream == &std::cerr) || (&stream == &std::clog))
    return stderr;

  return 0;
}

//namespace seq {

//const char     reset[] = "\033[0m";
//const char      bold[] = "\033[1m";
//const char      dark[] = "\033[2m";
//const char underline[] = "\033[4m";
//const char     blink[] = "\033[5m";
//const char   reverse[] = "\033[7m";
//const char concealed[] = "\033[8m";
//const char   crossed[] = "\033[9m";

//namespace fg {

//const char    grey[] = "\033[30m";
//const char     red[] = "\033[31m";
//const char   green[] = "\033[32m";
//const char  yellow[] = "\033[33m";
//const char    blue[] = "\033[34m";
//const char magenta[] = "\033[35m";
//const char    cyan[] = "\033[36m";
//const char   white[] = "\033[37m";
//const char   reset[] = "\033[39m";

//} // namespace fg

//namespace bg {

//const char    grey[] = "\033[40m";
//const char     red[] = "\033[41m";
//const char   green[] = "\033[42m";
//const char  yellow[] = "\033[43m";
//const char    blue[] = "\033[44m";
//const char magenta[] = "\033[45m";
//const char    cyan[] = "\033[46m";
//const char   white[] = "\033[47m";
//const char   reset[] = "\033[49m";

//} // namespace bg

//} // namespace seq

} // anonymous namespace

namespace ansi {

bool is_atty(const std::ostream& stream)
{
  FILE* std_stream = get_standard_stream(stream);

  // Unfortunately, fileno() ends with segmentation fault
  // if invalid file descriptor is passed. So we need to
  // handle this case gracefully and assume it's not a tty
  // if standard stream is not detected, and 0 is returned.
  if (!std_stream)
    return false;

  return ::isatty(fileno(std_stream));
}

bool enabled(std::ostream& stream)
{
  return is_atty(stream) || static_cast<bool>(stream.iword(ainsi_support_index));
};

std::ostream& enable(std::ostream& stream)
{
  stream.iword(ainsi_support_index) = 1L;
  return stream;
}

std::ostream& disable(std::ostream& stream)
{
  stream.iword(ainsi_support_index) = 0L;
  return stream;
}

const char* escape_sequence(ansi::style st) {
  switch(st) {
    case style::reset     : return "\033[0m";
    case style::bold      : return "\033[1m";
    case style::dark      : return "\033[2m";
    case style::underline : return "\033[4m";
    case style::blink     : return "\033[5m";
    case style::reverse   : return "\033[7m";
    case style::concealed : return "\033[8m";
    case style::crossed   : return "\033[9m";
    case style::grey_fg   : return "\033[30m";
    case style::red_fg    : return "\033[31m";
    case style::green_fg  : return "\033[32m";
    case style::yellow_fg : return "\033[33m";
    case style::blue_fg   : return "\033[34m";
    case style::magenta_fg: return "\033[35m";
    case style::cyan_fg   : return "\033[36m";
    case style::white_fg  : return "\033[37m";
    case style::default_fg: return "\033[39m";
    case style::grey_bg   : return "\033[40m";
    case style::red_bg    : return "\033[41m";
    case style::green_bg  : return "\033[42m";
    case style::yellow_bg : return "\033[43m";
    case style::blue_bg   : return "\033[44m";
    case style::magenta_bg: return "\033[45m";
    case style::cyan_bg   : return "\033[46m";
    case style::white_bg  : return "\033[47m";
    case style::default_bg: return "\033[49m";
  }
  return "";
}

std::ostream& operator<<(std::ostream& os, style st) {
  if (enabled(os))
    os << escape_sequence(st);
  return os;
}

} // namespace ainsi
