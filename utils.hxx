#ifndef UTILS_HXX
#define UTILS_HXX

#include <string>
#include <boost/filesystem.hpp>

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

#endif // UTILS_HXX
