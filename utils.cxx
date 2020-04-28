#include "utils.hxx"

#include <regex>
#include <algorithm>
#include <cctype>

#include <wordexp.h>

#include "query-utils.hxx"

namespace {

std::regex so_regex(R"(.*\.so(?:\.\d+)*$)");

} // anonymous namespace

bool starts_with(const std::string& str, const std::string& prefix) {
  if (str.length() >= prefix.length()) {
    return (0 == str.compare(0, prefix.length(), prefix));
  } else {
    return false;
  }
}

bool ends_with(const std::string& str, const std::string& suffix) {
  if (str.length() >= suffix.length()) {
    return (0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix));
  } else {
    return false;
  }
}

std::string expand_path(const std::string& in, const boost::filesystem::path& base) {
  wordexp_t wx;
  wordexp(in.c_str(), &wx, 0);
  std::string out(wx.we_wordv[0]);
  wordfree(&wx);

  return boost::filesystem::canonical(out, base).string();
}

const char* library_type(const std::string& value) {
  if (ends_with(value, ".a")) return "static";
  if (std::regex_match(value, so_regex)) return "shared";
  return "library";
}

const char* output_type(const std::string& value) {
  if (ends_with(value, ".o")) return "object";
  if (ends_with(value, ".a")) return "static";
  if (std::regex_match(value, so_regex)) return "shared";
  return "executable";
}

const char* input_type(const std::string& value) {
  if (ends_with(value, ".o")) return "object";
  if (ends_with(value, ".a")) return "static";
  if (std::regex_match(value, so_regex)) return "shared";
  return "source";
}

inline bool is_visible(const int c) {
  return std::isgraph(c);
}

void ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int c) { return std::isgraph(c); }));
}

void rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), [](int c) { return std::isgraph(c); }).base(), s.end());
}

void trim(std::string &s) {
  ltrim(s);
  rtrim(s);
}

std::string ltrim_copy(std::string s) {
  ltrim(s);
  return s;
}

std::string rtrim_copy(std::string s) {
  rtrim(s);
  return s;
}

std::string trim_copy(std::string s) {
  trim(s);
  return s;
}

std::string symbol_hname(const std::string& name, const std::string& dname) {
  return dname.empty() ? name : dname;
}

std::map<long long, std::string> get_symbol_hnames(Database2& db, const std::vector<long long>& ids)
{
  std::map<long long, std::string> names;

  std::stringstream ss;
  ss << R"(
select id, name, dname
from symbols
where id in )" << in_expr(ids);

  SQLite::Statement stm = db.statement(ss.str());

  while(stm.executeStep()) {
    names.emplace(
          stm.getColumn(0).getInt64(),
          symbol_hname(stm.getColumn(1).getString(), stm.getColumn(2).getString())
          );
  }

  return names;
}

std::vector<std::string> split(std::string str, const char delim) {
  std::vector<std::string> tokens;

  size_t pos = 0;
  while ((pos = str.find(delim)) != std::string::npos) {
      tokens.emplace_back(str.substr(0, pos));
      str.erase(0, pos + 1);
  }

  return tokens;
}
