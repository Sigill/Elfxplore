#include "linemarkers.hxx"

#include <regex>
#include <istream>

//#define VERBOSE_PARSER
#ifdef VERBOSE_PARSER
#include <iostream>
#endif

PreprocessedFile::PreprocessedFile(std::size_t line, std::string filename)
  : included_at_line(line)
  , filename(std::move(filename))
  , depth(0UL)
  , lines_count(0UL)
  , cumulated_lines_count(0UL)
  , last_effective_line(0UL)
{}

IncludeTree::IncludeTree()
  : files(1, PreprocessedFile(0, "-"))
  , root(&files.front())
{}

IncludeTree::IncludeTree(const IncludeTree& other)
  : files(other.files)
  , root(&files.front())
{
  relocate_files(other);
}

IncludeTree::IncludeTree(IncludeTree&& other)
  : files(std::move(other.files))
  , root(&files.front())
{}

IncludeTree& IncludeTree::operator=(const IncludeTree& other)
{
  files = other.files;
  root = &files.front();

  relocate_files(other);

  return *this;
}

IncludeTree& IncludeTree::operator=(IncludeTree&& other)
{
  files = std::move(other.files);
  root = other.root;
  return *this;
}

void IncludeTree::relocate_files(const IncludeTree& other)
{
  for(PreprocessedFile& file : files) {
    for(auto include : file.includes) {
      const auto it = std::find_if(other.files.cbegin(), other.files.cend(), [include](const PreprocessedFile& f){
        return &f == include;
      });
      const ptrdiff_t dst = std::distance(other.files.begin(), it);
      include = &files[dst];
    }
  }
}

namespace {

template <typename T, typename A>
bool contains(const std::vector<T, A>& haystack, const T& needle) {
  return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

class LineMarkersParser {
public:
  IncludeTree tree;
private:
  std::regex linemarker_regex;
  std::vector<PreprocessedFile*> stack;
  bool in_preamble;
  bool store_lines;

public:
  explicit LineMarkersParser(const bool store_lines);

#ifdef VERBOSE_PARSER
  void print_stack() const;
#endif

  void pop_stack();

  void push_stack(const std::string& filename);

  void finish();

  void parseLine(const std::string& line);
};

LineMarkersParser::LineMarkersParser(const bool store_lines)
  : tree()
  , linemarker_regex(R"EOR(^#\s+(\d+)\s+"([^"]*)"\s*(\d?)\s*(\d?)\s*(\d?))EOR")
  , stack(1, tree.root)
  , in_preamble(false)
  , store_lines(store_lines)
{}

#ifdef VERBOSE_PARSER
void LineMarkersParser::print_stack() const {
  std::cerr << "Stack: ";
  for(auto& i : stack)
    std::cerr << i->filename << " ";
  std::cerr << std::endl;
}
#endif

void LineMarkersParser::pop_stack() {
  const size_t include_cumulated_lines_count = (stack.back()->cumulated_lines_count += stack.back()->lines_count);
  stack.pop_back();
  stack.back()->cumulated_lines_count += include_cumulated_lines_count;
}

void LineMarkersParser::push_stack(const std::string& filename) {
  tree.files.emplace_back(stack.back()->last_effective_line, filename);
  PreprocessedFile& inserted = tree.files.back();
  stack.back()->includes.push_back(&inserted);
  stack.back()->includes.back()->depth = stack.back()->depth + 1;

  stack.back()->lines.emplace_back("#include \"" + filename + "\"");
  stack.emplace_back(stack.back()->includes.back());
}

void LineMarkersParser::finish() {
  while(stack.size() > 1)
    pop_stack();
}

void LineMarkersParser::parseLine(const std::string& line) {
#ifdef VERBOSE_PARSER
  std::cerr << "> " << line << std::endl;
#endif

  if (in_preamble) {
    in_preamble = line != "# 1 \"" + stack.back()->filename + "\"";

#ifdef VERBOSE_PARSER
    if (!in_preamble)
      std::cerr << "End of preamble" << std::endl;
    else
      std::cerr << "In preamble for " << stack.back()->filename << ", ignoring" << std::endl;
#endif

    return;
  }

  std::smatch m;
  if (std::regex_match(line, m, linemarker_regex)) {
    const size_t linenum = std::stoul(m[1].str());
    const std::string& filename = m[2].str();

    std::vector<int> flags;
    if (m[3].matched && !m[3].str().empty()) flags.push_back(std::stoi(m[3].str()));
    if (m[4].matched && !m[4].str().empty()) flags.push_back(std::stoi(m[4].str()));
    if (m[5].matched && !m[5].str().empty()) flags.push_back(std::stoi(m[5].str()));

    if (contains(flags, 1)) {
      PreprocessedFile* current = stack.back();
      current->last_effective_line += 1;
      current->lines_count += 1;

#ifdef VERBOSE_PARSER
      std::cerr << current->filename << ":" << stack.back()->last_effective_line << " includes " << filename << std::endl;
#endif

      push_stack(filename);

#ifdef VERBOSE_PARSER
      print_stack();
#endif
    } else if (contains(flags, 2)) {
#ifdef VERBOSE_PARSER
      const auto& prev = stack.back();
#endif

      pop_stack();
      stack.back()->last_effective_line = linenum-1; // linenum is the number of the following line

#ifdef VERBOSE_PARSER
      std::cerr << "Exiting " << prev->filename << ", returning to " << stack.back()->filename << ":" << stack.back()->last_effective_line << std::endl;
      print_stack();
#endif
    } else {
      if (filename == stack.back()->filename) { // Skipping multiple blank lines
        stack.back()->last_effective_line = linenum-1;
        if (store_lines)
          stack.back()->lines.push_back("#line " +  std::to_string(linenum));
      } else {
        while(stack.size() > 1)
          pop_stack();

        push_stack(filename);

        in_preamble = true;

#ifdef VERBOSE_PARSER
        std::cerr << "Adding root source file " << filename << std::endl;
        print_stack();
#endif
      }
    }
  } else {
#ifdef VERBOSE_PARSER
    std::cerr << "Non linemarker" << std::endl;
#endif
    stack.back()->last_effective_line += 1;
    stack.back()->lines_count += 1;
    if (store_lines)
      stack.back()->lines.push_back(line);
  }
}

} // anonymous namespace

IncludeTree build_include_tree(std::istream& in, const bool store_lines)
{
  LineMarkersParser parser(store_lines);

  std::string line;
  while(std::getline(in, line)) {
    parser.parseLine(line);
  }

  parser.finish();

  return std::move(parser.tree);
}

void preorder_walk(const IncludeTree& tree, std::function<void(const PreprocessedFile&)> cbk) {
  std::vector<const PreprocessedFile*> queue;
  queue.reserve(16);
  queue.insert(queue.begin(), tree.root->includes.rbegin(), tree.root->includes.rend());

  while(!queue.empty()) {
    const PreprocessedFile* current = queue.back();
    queue.pop_back();
    cbk(*current);
    queue.reserve(queue.size() + current->includes.size());
    queue.insert(queue.end(), current->includes.rbegin(), current->includes.rend());
  }
}
