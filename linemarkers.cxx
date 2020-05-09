#include "linemarkers.hxx"

#include <regex>
#include <list>
#include <istream>

//#define VERBOSE_PARSER
#ifdef VERBOSE_PARSER
#include <iostream>
#endif

PreprocessedFile::PreprocessedFile(std::string filename)
  : filename(std::move(filename))
  , last_line(0UL)
{}

IncludeTree::IncludeTree()
  : files(1, PreprocessedFile("-"))
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

IncludeTree&IncludeTree::operator=(const IncludeTree& other)
{
  files = other.files;
  root = &files.front();

  relocate_files(other);

  return *this;
}

IncludeTree&IncludeTree::operator=(IncludeTree&& other)
{
  files = std::move(other.files);
  root = other.root;
  return *this;
}

void IncludeTree::relocate_files(const IncludeTree& other)
{
  for(size_t i = 0; i < files.size(); ++i) {
    for(std::pair<size_t, PreprocessedFile*>& include : files[i].includes) {
      const auto it = std::find_if(other.files.cbegin(), other.files.cend(), [include=include.second](const PreprocessedFile& f){
        return &f == include;
      });
      const ptrdiff_t dst = std::distance(other.files.begin(), it);
      include.second = &files[dst];
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
  std::regex linemarker_regex;
  std::vector<PreprocessedFile*> stack;
  bool in_preamble;

  LineMarkersParser()
    : tree()
    , linemarker_regex(R"EOR(^#\s+(\d+)\s+"([^"]*)"\s*(\d?)\s*(\d?)\s*(\d?))EOR")
    , stack(1, tree.root)
    , in_preamble(false)
  {}

#ifdef VERBOSE_PARSER
  void print_stack() const {
    std::cerr << "Stack: ";
    for(auto& i : stack)
      std::cerr << i->filename << " ";
    std::cerr << std::endl;
  }
#endif

  void parseLine(const std::string& line) {
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
        tree.files.emplace_back(filename);
        PreprocessedFile& inserted = tree.files.back();
        stack.back()->last_line += 1;
        stack.back()->includes.push_back({stack.back()->last_line, &inserted});

#ifdef VERBOSE_PARSER
        std::cerr << stack.back()->filename << ":" << stack.back()->last_line << " includes " << filename << std::endl;
#endif

        stack.back()->lines.emplace_back("#include \"" + filename + "\"");
        stack.emplace_back(stack.back()->includes.back().second);

#ifdef VERBOSE_PARSER
        print_stack();
#endif
      } else if (contains(flags, 2)) {
#ifdef VERBOSE_PARSER
        const auto& prev = stack.back();
#endif

        stack.pop_back();
        stack.back()->last_line = linenum-1; // linenum is the number of the following line

#ifdef VERBOSE_PARSER
        std::cerr << "Exiting " << prev->filename << ", returning to " << stack.back()->filename << ":" << stack.back()->last_line << std::endl;
        print_stack();
#endif
      } else {
        if (filename == stack.back()->filename) { // Skipping multiple blank lines
          stack.back()->last_line = linenum-1;
          stack.back()->lines.push_back("#line " +  std::to_string(linenum));
        } else {
          while(stack.size() > 1)
            stack.pop_back();

          tree.files.emplace_back(filename);
          PreprocessedFile& inserted = tree.files.back();
          stack.back()->lines.emplace_back("#include \"" + filename + "\"");
          stack.back()->includes.push_back({stack.back()->last_line, &inserted});
          stack.emplace_back(stack.back()->includes.back().second);
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
      stack.back()->last_line += 1;
      stack.back()->lines.push_back(line);
    }
  }
};

} // anonymous namespace

IncludeTree build_include_tree(std::istream& in)
{
  LineMarkersParser parser;

  std::string line;
  while(std::getline(in, line)) {
    parser.parseLine(line);
  }

  return std::move(parser.tree);
}

std::vector<Include> linearize(const IncludeTree& tree) {
  std::list<std::tuple<const PreprocessedFile*, size_t, size_t>> ltree;
  //                                            line    depth

  for(const auto& inc : tree.root->includes)
    ltree.emplace_back(inc.second, 0UL, 0UL);

  for(auto it = ltree.begin(); it != ltree.end(); ++it) {
    auto next = std::next(it);
    for(const auto& inc : std::get<0>(*it)->includes) {
      ltree.emplace(next, std::make_tuple(inc.second, inc.first, std::get<2>(*it) + 1));
    }
  }

  std::vector<Include> lltree;
  lltree.reserve(ltree.size());
  for(const auto& n : ltree)
    lltree.emplace_back(std::get<0>(n)->filename, std::get<1>(n), std::get<2>(n), std::get<0>(n)->last_line);

  return lltree;
}
