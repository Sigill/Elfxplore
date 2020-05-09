#ifndef LINEMARKERS_HXX
#define LINEMARKERS_HXX

#include <vector>
#include <deque>
#include <string>
#include <iosfwd>

class PreprocessedFile {
public:
  std::string filename;
  std::vector<std::string> lines;
  std::vector<std::pair<size_t, PreprocessedFile*>> includes;
  std::size_t last_line;

  explicit PreprocessedFile(std::string filename);
};

class IncludeTree {
public:
  std::deque<PreprocessedFile> files;
  PreprocessedFile* root;

  IncludeTree();

  IncludeTree(const IncludeTree& other);
  IncludeTree(IncludeTree&& other);

  IncludeTree& operator=(const IncludeTree& other);
  IncludeTree& operator=(IncludeTree&& other);

private:
  void relocate_files(const IncludeTree& other);
};

IncludeTree build_include_tree(std::istream& in);

class Include {
public:
  std::string filename;
  size_t line;
  size_t depth;
  size_t lines_count;

  Include(std::string filename, size_t line, size_t depth, size_t lines_count)
    : filename(std::move(filename))
    , line(line)
    , depth(depth)
    , lines_count(lines_count)
  {}
};

std::vector<Include> linearize(const IncludeTree& tree);

#endif // LINEMARKERS_HXX
