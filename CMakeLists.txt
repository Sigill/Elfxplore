cmake_minimum_required(VERSION 2.8.12)
project(Elfxplore)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(OpenMP REQUIRED)

include(ExternalProject)

ExternalProject_Add(termcolor_project
  GIT_REPOSITORY git://github.com/ikalnytskyi/termcolor.git
  GIT_TAG v1.0.1

  # Termcolor is a header-only library which means we need to
  # neither configure nor build nor install it. Thus, noop
  # the hooks.
  CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")
ExternalProject_Get_Property(termcolor_project SOURCE_DIR)
set(TERMCOLOR_INCLUDE_DIR ${SOURCE_DIR}/include)

add_executable(elfxplore
    logger.cxx
    SymbolReference.cxx
    SymbolReferenceSet.cxx
    nm.cxx
    Database2.cxx
    utils.cxx
    query-utils.cxx
    command-utils.cxx
    task.cxx
    db-task.cxx
    command-task.cxx
    extract-task.cxx
    dependencies-task.cxx
    analyse-task.cxx
    artifacts-task.cxx
    elfxplore.cxx)
target_include_directories(elfxplore PRIVATE ${TERMCOLOR_INCLUDE_DIR})
target_link_libraries(elfxplore Boost::system Boost::program_options Boost::filesystem Threads::Threads SQLite::SQLite3 SQLiteCpp OpenMP::OpenMP_CXX)
