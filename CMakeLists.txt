cmake_minimum_required(VERSION 2.8.12)
project(Elfxplore)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

set (CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Shellwords 0.2 REQUIRED)
find_package(Linemarkers 0.1 REQUIRED)
find_package(Instrmt REQUIRED)

include(ExternalProject)

ExternalProject_Add(jhasse_ThreadPool_project
    GIT_REPOSITORY git://github.com/jhasse/ThreadPool.git
    GIT_TAG e9123ff
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_CXX_COMPILER} -O2 -std=c++17 -c ThreadPool.cpp -o ThreadPool.cpp.o && ${CMAKE_AR} qc ThreadPool.a ThreadPool.cpp.o
    INSTALL_COMMAND "")
ExternalProject_Get_Property(jhasse_ThreadPool_project SOURCE_DIR)
set(jhasse_ThreadPool_DIR ${SOURCE_DIR})
add_library(jhasse_ThreadPool STATIC IMPORTED)
set_target_properties(jhasse_ThreadPool PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${jhasse_ThreadPool_DIR}"
    IMPORTED_LINK_INTERFACE_LIBRARIES "Threads::Threads"
    IMPORTED_LOCATION "${jhasse_ThreadPool_DIR}/ThreadPool.a"
    )
add_dependencies(jhasse_ThreadPool jhasse_ThreadPool_project)

option(ENABLE_INSTRUMENTATION "Enable internal log messages" OFF)
function(target_link_instrmt tgt)
    if (ENABLE_INSTRUMENTATION)
        target_link_libraries(${tgt} PRIVATE instrmt)
        target_compile_definitions(${tgt} PRIVATE INSTRMT_ENABLE)
    else()
        target_include_directories(${tgt} PRIVATE $<TARGET_PROPERTY:instrmt,INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endfunction()

set(FILESYSTEM_LIBRARY "stdc++fs")

include(CTest)

add_subdirectory(core)
add_subdirectory(cli)
