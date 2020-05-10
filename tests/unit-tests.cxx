#include <gmock/gmock.h>

#include <shellwords/shellwords.hxx>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include "command-utils.hxx"
#include "linemarkers.hxx"
#include "utils.hxx"

#include <fstream>

namespace bfs = boost::filesystem;
namespace bp = boost::process;

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
    parse_command(line, command);

    EXPECT_EQ(command.directory, "/some/directory");
    EXPECT_EQ(command.executable, "gcc");
    EXPECT_EQ(command.args, "-o object.o -c source.c");
    EXPECT_EQ(command.output, "object.o");
    EXPECT_EQ(command.output_type, "object");
  }

  {
    const char* line = R"("/some/directory with spaces" ar qc static.a object.o)";

    CompilationCommand command;
    parse_command(line, command);

    EXPECT_EQ(command.directory, "/some/directory with spaces");
    EXPECT_EQ(command.executable, "ar");
    EXPECT_EQ(command.args, "qc static.a object.o");
    EXPECT_EQ(command.output, "static.a");
    EXPECT_EQ(command.output_type, "static");
  }
}

void write_file(const bfs::path& path, const char *data) {
  std::ofstream of(path.string());
  of << data;
  of.close();
}

TEST(elfxplore, linemarkers) {
  const bfs::path dir(bfs::unique_path(bfs::temp_directory_path() / "%%%%-%%%%-%%%%-%%%%"));
  const TempFileGuard g(dir);
  bfs::create_directory(dir);

  write_file(dir / "a.h", R"(#ifndef A_H
#define A_H
void a3();

#endif /* A_H */
)");

  write_file(dir / "b.h", R"(#ifndef B_H
#define B_H

void b4();

#endif /* B_H */
)");

  write_file(dir / "c.h", R"(#ifndef C_H
#define C_H
#include "a.h"








#include "b.h"
void c13();

#endif /* C_H */
)");

  write_file(dir / "d.cpp", R"(#include "c.h"
void d2();
)");

  write_file(dir / "e.cpp", R"(void e1();
)");

  const char cmd[] = "g++ -I. -E d.cpp e.cpp";

  bp::ipstream out_stream, err_stream;

  bp::child p(cmd,
              bp::start_dir = dir,
              bp::std_in.close(),
              bp::std_out > out_stream,
              bp::std_err > err_stream
              );

  std::future<IncludeTree> tree_f = std::async(std::launch::async, [&out_stream](){
    return build_include_tree(out_stream);
  });

  std::future<std::string> err_f = std::async(std::launch::async, [&err_stream](){
    std::ostringstream ss;
    ss << err_stream.rdbuf();
    return ss.str();
  });

  p.wait();

  EXPECT_EQ(err_f.get(), "");

  const IncludeTree tree = tree_f.get();
  ASSERT_EQ(tree.files.size(), 6UL);
  EXPECT_EQ(tree.files[0].filename, "-");
  EXPECT_EQ(tree.files[1].filename, "d.cpp");
  EXPECT_EQ(tree.files[2].filename, "c.h");
  EXPECT_EQ(tree.files[3].filename, "a.h");
  EXPECT_EQ(tree.files[4].filename, "b.h");
  EXPECT_EQ(tree.files[5].filename, "e.cpp");

  const auto& a = tree.files[3];
  EXPECT_EQ(a.included_at_line, 3UL);
  EXPECT_EQ(a.depth, 3UL);
  EXPECT_EQ(a.last_effective_line, 3UL);
  EXPECT_EQ(a.lines_count, 3UL);
  EXPECT_EQ(a.cumulated_lines_count, 3UL);
  EXPECT_THAT(a.includes, ::testing::IsEmpty());
  EXPECT_THAT(a.lines, ::testing::ElementsAre("",
                                              "",
                                              "void a3();"));

  const auto& b = tree.files[4];
  EXPECT_EQ(b.included_at_line, 12UL);
  EXPECT_EQ(b.depth, 3UL);
  EXPECT_EQ(b.last_effective_line, 4UL);
  EXPECT_EQ(b.lines_count, 4UL);
  EXPECT_EQ(b.cumulated_lines_count, b.lines_count);
  EXPECT_THAT(b.includes, ::testing::IsEmpty());
  EXPECT_THAT(b.lines, ::testing::ElementsAre("",
                                              "",
                                              "",
                                              "void b4();"));

  const auto& c = tree.files[2];
  EXPECT_EQ(c.included_at_line, 1UL);
  EXPECT_EQ(c.depth, 2UL);
  EXPECT_EQ(c.last_effective_line, 13UL);
  EXPECT_EQ(c.lines_count, 5UL);
  EXPECT_EQ(c.cumulated_lines_count, c.lines_count + a.cumulated_lines_count + b.cumulated_lines_count);
  EXPECT_THAT(c.includes, ::testing::ElementsAre(&tree.files[3], &tree.files[4]));
  EXPECT_THAT(c.lines, ::testing::ElementsAre("",
                                              "",
                                              "#include \"a.h\"",
                                              "#line 12",
                                              "#include \"b.h\"",
                                              "void c13();"));

  const auto& d = tree.files[1];
  EXPECT_EQ(d.included_at_line, 0UL);
  EXPECT_EQ(d.depth, 1UL);
  EXPECT_EQ(d.last_effective_line, 2UL);
  EXPECT_EQ(d.lines_count, 2UL);
  EXPECT_EQ(d.cumulated_lines_count, d.lines_count + c.cumulated_lines_count);
  EXPECT_THAT(d.includes, ::testing::ElementsAre(&tree.files[2]));
  EXPECT_THAT(d.lines, ::testing::ElementsAre("#include \"c.h\"",
                                              "void d2();"));

  const auto& e = tree.files[5];
  EXPECT_EQ(e.included_at_line, 0UL);
  EXPECT_EQ(e.depth, 1UL);
  EXPECT_EQ(e.last_effective_line, 1UL);
  EXPECT_EQ(e.lines_count, 1UL);
  EXPECT_EQ(e.cumulated_lines_count, e.cumulated_lines_count);
  EXPECT_THAT(e.includes, ::testing::IsEmpty());
  EXPECT_THAT(e.lines, ::testing::ElementsAre("void e1();"));

  const auto& _ = tree.files[0];
  EXPECT_EQ(_.included_at_line, 0UL);
  EXPECT_EQ(_.depth, 0UL);
  EXPECT_EQ(_.last_effective_line, 0UL);
  EXPECT_EQ(_.lines_count, 0UL);
  EXPECT_EQ(_.cumulated_lines_count, _.lines_count + d.cumulated_lines_count + e.cumulated_lines_count);
  EXPECT_THAT(_.includes, ::testing::ElementsAre(&tree.files[1], &tree.files[5]));
  EXPECT_THAT(_.lines, ::testing::ElementsAre("#include \"d.cpp\"",
                                              "#include \"e.cpp\""));

  std::vector<const PreprocessedFile*> llview;
  preorder_walk(tree, [&llview](const PreprocessedFile& file){ llview.push_back(&file); });
  ASSERT_EQ(llview.size(), 5UL);
  EXPECT_EQ(llview[0]->filename, "d.cpp");
  EXPECT_EQ(llview[1]->filename, "c.h");
  EXPECT_EQ(llview[2]->filename, "a.h");
  EXPECT_EQ(llview[3]->filename, "b.h");
  EXPECT_EQ(llview[4]->filename, "e.cpp");
}
