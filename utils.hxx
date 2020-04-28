#ifndef UTILS_HXX
#define UTILS_HXX

#include <string>
#include <vector>
#include <map>
#include <boost/filesystem.hpp>

#include "Database2.hxx"

bool starts_with(const std::string& str, const std::string& prefix);
bool ends_with(const std::string& str, const std::string& suffix);

std::string expand_path(const std::string& in, const boost::filesystem::path& base = boost::filesystem::current_path());

const char* library_type(const std::string& value);

const char* output_type(const std::string& value);

const char* input_type(const std::string& value);

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

#endif // UTILS_HXX
