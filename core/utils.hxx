#ifndef UTILS_HXX
#define UTILS_HXX

#include <string>
#include <vector>
#include <map>
#include <filesystem>

class Database2;

bool starts_with(const std::string& str, const std::string& prefix);
bool ends_with(const std::string& str, const std::string& suffix);

std::filesystem::path expand_path(const std::string& in, const std::filesystem::path& base);

std::filesystem::path expand_path(const std::string& in);

const char* get_library_type(const std::string& value);

const char* get_output_type(const std::string& value);

const char* get_input_type(const std::string& value);

const char* artifact_type(const std::string& value);

void ltrim(std::string &s);

void rtrim(std::string &s);

void trim(std::string &s);

std::string ltrim_copy(std::string s);

std::string rtrim_copy(std::string s);

std::string trim_copy(std::string s);

std::string symbol_hname(const std::string& name, const std::string& dname);

std::map<long long, std::string> get_symbol_hnames(Database2& db, const std::vector<long long>& ids);

std::vector<std::string> split(std::string str, const char delim);

void wc(std::istream& in, size_t& c, size_t& l);

void wc(const std::string& file, size_t& c, size_t& l);

std::string random_alnum(std::string::size_type length);

std::string which(const std::string& executable, bool &found);

class FileSystemGuard {
private:
  std::filesystem::path mPath;
public:
  explicit FileSystemGuard(const std::filesystem::path& p);
  ~FileSystemGuard();

  const std::filesystem::path& path() const;
};

namespace io {

class repeat {
public:
  const std::string& value;
  const size_t n;
  repeat(const std::string& value, size_t n)
    : value(value), n(n) {}
};

std::ostream& operator<<(std::ostream& os, const io::repeat& m);

} // namespace io

#endif // UTILS_HXX
