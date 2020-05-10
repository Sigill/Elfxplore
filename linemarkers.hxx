#ifndef LINEMARKERS_HXX
#define LINEMARKERS_HXX

#include <vector>
#include <deque>
#include <string>
#include <iosfwd>
#include <functional>

class PreprocessedFile {
public:
  std::size_t included_at_line;
  std::string filename;
  std::vector<std::string> lines;
  std::vector<PreprocessedFile*> includes;
  std::size_t depth;
  std::size_t lines_count, cumulated_lines_count;
  std::size_t last_effective_line;

  explicit PreprocessedFile(std::size_t included_at_line, std::string filename);
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

IncludeTree build_include_tree(std::istream& in, const bool store_lines = true);

void preorder_walk(const IncludeTree& tree, std::function<void(const PreprocessedFile&)> cbk);

#endif // LINEMARKERS_HXX
