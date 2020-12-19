#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <string.h>

#include <shellwords/shellwords.hxx>

#include <boost/program_options.hpp>

#include "command-utils.hxx"
#include "utils.hxx"
#include "nm.hxx"

namespace fs = std::filesystem;

namespace {

fs::path create_temporary_directory()
{
  const fs::path tmp_dir = std::filesystem::temp_directory_path();

  fs::path path = tmp_dir / "XXXXXX";
  char tmp[FILENAME_MAX];
  strncpy(tmp, path.c_str(), FILENAME_MAX-1);

  if (mkdtemp(tmp)) {
    return fs::path(tmp);
  }

  throw std::runtime_error(std::string("Unable to create a temporary directory: ") + std::strerror(errno));
}

void write_file(const fs::path& path, const char *data) {
  std::ofstream of(path.string());
  of << data;
  of.close();
}

} // anonymous namespace

TEST(elfxplore, split_command) {
  // That's not what we want.
  EXPECT_THAT(boost::program_options::split_unix(R"(c++ '-DPYTHON="2.7"')"),
              ::testing::ElementsAre("c++", R"(-DPYTHON=2.7)"));

  EXPECT_THAT(shellwords::shellsplit(R"(c++ '-DPYTHON="2.7"')"),
              ::testing::ElementsAre("c++", R"(-DPYTHON="2.7")"));

  EXPECT_THAT(shellwords::shellsplit(R"(c++ '-DPYTHON="2.7"')"),
              ::testing::ElementsAre("c++", R"(-DPYTHON="2.7")"));
}

TEST(elfxplore, parse_command) {
  {
    const char* line = "/some/directory gcc -o object.o -c source.c";

    CompilationCommand command;
    parse_command(line, command, parse_command_options::with_directory);

    EXPECT_EQ(command.directory, "/some/directory");
    EXPECT_EQ(command.executable, "gcc");
    EXPECT_EQ(command.args, "-o object.o -c source.c");
    EXPECT_EQ(command.output, "object.o");
    EXPECT_EQ(command.output_type, "object");
  }

  {
    const char* line = R"("/some/directory with spaces" ar qc static.a object.o)";

    CompilationCommand command;
    parse_command(line, command, parse_command_options::with_directory);

    EXPECT_EQ(command.directory, "/some/directory with spaces");
    EXPECT_EQ(command.executable, "ar");
    EXPECT_EQ(command.args, "qc static.a object.o");
    EXPECT_EQ(command.output, "static.a");
    EXPECT_EQ(command.output_type, "static");
  }
}

void PrintTo(const SymbolReference& symbol, std::ostream* os) {
  *os << '{' << symbol.address << ' ' << symbol.type << ' ' << symbol.name << ' ' << symbol.size << '}';
}

class ContainsSymbolMatcher : public ::testing::MatcherInterface<const SymbolReferenceSet&> {
public:
  explicit ContainsSymbolMatcher(std::string needle) : needle(std::move(needle)) {}

  bool MatchAndExplain(const SymbolReferenceSet& symbols, ::testing::MatchResultListener* /*l*/) const override {
    return std::find_if(symbols.begin(), symbols.end(), [this](const SymbolReference& symbol){ return symbol.name == needle; }) != symbols.end();
  }

  void DescribeTo(::std::ostream* os) const override {
    *os << "contains the \"" << needle << "\" symbol";
  }

  void DescribeNegationTo(::std::ostream* os) const override {
    *os << "does not contain the \"" << needle << "\" symbol";
  }

private:
  std::string needle;
};

::testing::Matcher<const SymbolReferenceSet&> ContainsSymbol(std::string name) {
  return ::testing::MakeMatcher(new ContainsSymbolMatcher(std::move(name)));
}

TEST(elfxplore, nm) {
  const fs::path dir = create_temporary_directory();

  const FileSystemGuard g(dir);
  const fs::path a_c = dir / "a.c";
  const fs::path b_c = dir / "b.c";
  const fs::path a_so = dir / "liba.so";
  const fs::path b_so = dir / "libb.so";

  write_file(a_c, "int a() { return 0; }");
  write_file(b_c, R"(
int a();
static int b() { return a(); }
int c() { return a(); }
)");

  const std::string cmd_a = "gcc -shared -o " + a_so.string() + " " + a_c.string();
  const std::string cmd_b = "gcc -shared -o " + b_so.string() + " -L" + dir.string() + " -la " + b_c.string();

  ASSERT_EQ(system(cmd_a.c_str()), 0);
  ASSERT_EQ(system(cmd_b.c_str()), 0);

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::undefined);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ContainsSymbol("a"));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("c")));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ContainsSymbol("b"));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_extern);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::undefined_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ContainsSymbol("a"));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("c")));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_extern_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }

  const std::string strip_cmd = "strip -s " + b_so.string();
  ASSERT_EQ(system(strip_cmd.c_str()), 0);

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::undefined);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::IsEmpty());
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::IsEmpty());
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_extern);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::IsEmpty());
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::undefined_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ContainsSymbol("a"));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("c")));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }

  {
    SymbolReferenceSet symbols;
    ProcessResult result = nm(b_so.string(), symbols, nm_options::defined_extern_dynamic);
    EXPECT_EQ(result.code, 0);
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("a")));
    EXPECT_THAT(symbols, ::testing::Not(ContainsSymbol("b")));
    EXPECT_THAT(symbols, ContainsSymbol("c"));
  }
}
