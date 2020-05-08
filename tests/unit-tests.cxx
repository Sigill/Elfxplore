#include <gmock/gmock.h>

#include <shellwords/shellwords.hxx>

#include <boost/program_options.hpp>

#include "command-utils.hxx"

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
