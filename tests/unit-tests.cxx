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

  EXPECT_EQ(tree.files[0].last_line, 0UL);
  ASSERT_EQ(tree.files[0].includes.size(), 2UL);
  EXPECT_EQ(tree.files[0].includes[0].first, 0UL);
  EXPECT_EQ(tree.files[0].includes[0].second, &tree.files[1]);
  EXPECT_EQ(tree.files[0].includes[1].first, 0UL);
  EXPECT_EQ(tree.files[0].includes[1].second, &tree.files[5]);
  EXPECT_THAT(tree.files[0].lines, ::testing::ElementsAre("#include \"d.cpp\"",
                                                          "#include \"e.cpp\""));

  EXPECT_EQ(tree.files[1].last_line, 2UL);
  ASSERT_EQ(tree.files[1].includes.size(), 1UL);
  EXPECT_EQ(tree.files[1].includes[0].first, 1UL);
  EXPECT_EQ(tree.files[1].includes[0].second, &tree.files[2]);
  EXPECT_THAT(tree.files[1].lines, ::testing::ElementsAre("#include \"c.h\"",
                                                          "void d2();"));

  EXPECT_EQ(tree.files[2].last_line, 13UL);
  ASSERT_EQ(tree.files[2].includes.size(), 2UL);
  EXPECT_EQ(tree.files[2].includes[0].first, 3UL);
  EXPECT_EQ(tree.files[2].includes[0].second, &tree.files[3]);
  EXPECT_EQ(tree.files[2].includes[1].first, 12UL);
  EXPECT_EQ(tree.files[2].includes[1].second, &tree.files[4]);
  EXPECT_THAT(tree.files[2].lines, ::testing::ElementsAre("",
                                                          "",
                                                          "#include \"a.h\"",
                                                          "#line 12",
                                                          "#include \"b.h\"",
                                                          "void c13();"));

  EXPECT_EQ(tree.files[3].last_line, 3UL);
  ASSERT_EQ(tree.files[3].includes.size(), 0UL);
  EXPECT_THAT(tree.files[3].lines, ::testing::ElementsAre("",
                                                          "",
                                                          "void a3();"));

  EXPECT_EQ(tree.files[4].last_line, 4UL);
  ASSERT_EQ(tree.files[4].includes.size(), 0UL);
  EXPECT_THAT(tree.files[4].lines, ::testing::ElementsAre("",
                                                          "",
                                                          "",
                                                          "void b4();"));

  EXPECT_EQ(tree.files[5].last_line, 1UL);
  ASSERT_EQ(tree.files[5].includes.size(), 0UL);
  EXPECT_THAT(tree.files[5].lines, ::testing::ElementsAre("void e1();"));
}
