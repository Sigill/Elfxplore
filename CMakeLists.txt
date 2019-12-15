cmake_minimum_required(VERSION 2.8.12)
project(Elfxplore)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(OpenMP REQUIRED)

add_executable(elfxplore
    SymbolReference.cxx
    SymbolReferenceSet.cxx
    nm.cxx
    Database2.cxx
    utils.cxx
    query-utils.cxx
    command.cxx
    db-command.cxx
    extract-dependencies-command.cxx
    export-dependencies-command.cxx
    extract-symbols-command.cxx
    analyse-symbols-command.cxx
    elfxplore.cxx)
target_link_libraries(elfxplore Boost::system Boost::program_options Boost::filesystem Threads::Threads SQLite::SQLite3 SQLiteCpp OpenMP::OpenMP_CXX)
