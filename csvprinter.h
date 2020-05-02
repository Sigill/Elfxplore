#pragma once
#include <string>
#include <iostream>
#include <sstream>

// Adapted from https://gist.github.com/rudolfovich/f250900f1a833e715260a66c87369d15

namespace csv {

class printer;

inline static printer& endrow(printer& csv);
inline static printer& flush(printer& csv);
inline static printer& empty(printer& csv);

class printer
{
  std::ostream& os_;
  bool is_first_;
  const std::string separator_;
  const std::string escape_seq_;
  const std::string special_chars_;
public:
  printer(std::ostream& out, std::string separator = ";")
    : os_(out)
    , is_first_(true)
    , separator_(std::move(separator))
    , escape_seq_("\"")
    , special_chars_("\"")
  {
  }

  ~printer() = default;

  void flush()
  {
    os_.flush();
  }

  void endrow()
  {
    os_ << std::endl;
    is_first_ = true;
  }

  template<typename T>
  printer& write (const T& val)
  {
    if (!is_first_)
    {
      os_ << separator_;
    }
    else
    {
      is_first_ = false;
    }
    os_ << val;
    return *this;
  }

  printer& write_escape(const std::string & val)
  {
    std::ostringstream result;
    result << '"';
    std::string::size_type to, from = 0u, len = val.length();
    while (from < len &&
           std::string::npos != (to = val.find_first_of(special_chars_, from)))
    {
      result << val.substr(from, to - from) << escape_seq_ << val[to];
      from = to + 1;
    }
    result << val.substr(from) << '"';

    return write(result.str());
  }
};


printer& operator<<(printer& csv, printer& (* val)(printer&))
{
  return val(csv);
}

printer& operator<<(printer& csv, const char * val)
{
  return csv.write_escape(val);
}

printer& operator<<(printer& csv, const std::string & val)
{
  return csv.write_escape(val);
}

template<typename T>
printer& operator<<(printer& csv, const T& val)
{
  return csv.write(val);
}

inline static printer& endrow(printer& csv)
{
  csv.endrow();
  return csv;
}

inline static printer& flush(printer& csv)
{
  csv.flush();
  return csv;
}

inline static printer& empty(printer& csv)
{
  csv.write("");
  return csv;
}

} // namespace csv
