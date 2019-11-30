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

#endif // UTILS_HXX
