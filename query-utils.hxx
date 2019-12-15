#ifndef QUERYUTILS_HXX
#define QUERYUTILS_HXX

#include <string>
#include <vector>
#include <iterator>
#include <algorithm>

struct quoted_string_literal_manip
{
  const std::string& value;
  explicit quoted_string_literal_manip(const std::string& value) : value(value) {}
};

quoted_string_literal_manip quoted_string_literal(const std::string& value);

std::ostream& operator<<(std::ostream& out, const quoted_string_literal_manip& m);

template<typename T>
struct csv_expr_manip
{
  const std::vector<T>& values;
  const char* separator;
  explicit csv_expr_manip(const std::vector<T>& values, const char* separator)
    : values(values)
    , separator(separator)
  {}
};

template<typename T>
csv_expr_manip<T> csv_expr(const std::vector<T>& values, const char* separator) {
  return csv_expr_manip<T>(values, separator);
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const csv_expr_manip<T>& m);

template<>
std::ostream& operator<<(std::ostream& out, const csv_expr_manip<std::string>& m);

template<typename T>
struct in_expr_manip
{
  const std::vector<T>& values;
  explicit in_expr_manip(const std::vector<T>& values)
    : values(std::move(values))
  {}
};

template<typename T>
in_expr_manip<T> in_expr(const std::vector<T>& values) {
  return in_expr_manip<T>(values);
}

template<typename T>
std::ostream& operator<<(std::ostream& out, const in_expr_manip<T>& m) {
  return out << "(" << csv_expr_manip<T>(m.values, ", ") << ")";
}

#endif // QUERYUTILS_HXX
