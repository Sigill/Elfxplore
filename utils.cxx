#include "utils.hxx"

#include <regex>

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

std::string expand_path(const std::string& in) {
  if (in.empty()) return in;

  if (in[0] == '~') {
    const char* home = getenv("HOME");
    if (home == nullptr) {
      throw std::invalid_argument("error: HOME environment variable not defined.");
    }

    return (boost::filesystem::path(home) / in.substr(1)).string();
  } else {
    return in;
  }
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
