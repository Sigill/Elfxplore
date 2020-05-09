cmake_minimum_required(VERSION 2.8.12)
project(Elfxplore)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Shellwords 0.2 REQUIRED)

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

add_library(elfxplore-core STATIC
    logger.cxx
    SymbolReference.cxx
    SymbolReferenceSet.cxx
    nm.cxx
    Database2.cxx
    utils.cxx
    query-utils.cxx
    command-utils.cxx
    linemarkers.cxx)
target_include_directories(elfxplore-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(elfxplore-core
    PUBLIC SQLiteCpp Boost::system Boost::program_options Boost::filesystem
    PRIVATE SQLite::SQLite3 Threads::Threads OpenMP::OpenMP_CXX shellwords)

add_executable(elfxplore
    progressbar.cxx
    task.cxx
    tasks/db-task.cxx
    tasks/command-task.cxx
    tasks/extract-task.cxx
    tasks/dependencies-task.cxx
    tasks/analyse-task.cxx
    tasks/artifacts-task.cxx
    elfxplore.cxx)
target_include_directories(elfxplore PRIVATE ${TERMCOLOR_INCLUDE_DIR})
target_link_libraries(elfxplore elfxplore-core Boost::system Boost::program_options Boost::filesystem Threads::Threads SQLiteCpp OpenMP::OpenMP_CXX shellwords)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest REQUIRED NO_MODULE)
    enable_testing()

    add_executable(unit-tests tests/unit-tests.cxx)
      target_link_libraries(unit-tests
        PRIVATE elfxplore-core
                shellwords
                GTest::gmock_main
                Boost::program_options)
      add_test(unit-tests unit-tests)
endif()
