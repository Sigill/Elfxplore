#include "query-utils.hxx"

#include <iomanip>
#include <iterator>
#include <algorithm>

quoted_string_literal_manip quoted_string_literal(const std::string& value) {
  return quoted_string_literal_manip(value);
}

std::ostream&operator<<(std::ostream& out, const quoted_string_literal_manip& m) {
  return out << std::quoted(m.value.c_str(), '\'', '\'');
}

template<>
std::ostream& operator<<(std::ostream& out, const csv_expr_manip<std::string>& m) {
  if (m.values.size() > 1) std::transform(m.values.cbegin(), --m.values.cend(),
                                          std::ostream_iterator<quoted_string_literal_manip>(out, m.separator),
                                          quoted_string_literal);
  if (m.values.size() > 0) out << quoted_string_literal_manip(m.values.back());
  return out;
}
