cmake_minimum_required(VERSION 2.8.12)
project(Elfxplore)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Shellwords 0.2 REQUIRED)
find_package(Linemarkers 0.1 REQUIRED)

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


ExternalProject_Add(progschj_ThreadPool
    GIT_REPOSITORY git://github.com/progschj/ThreadPool.git
    GIT_TAG 9a42ec1
    CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")
ExternalProject_Get_Property(progschj_ThreadPool SOURCE_DIR)
set(progschj_ThreadPool_DIR ${SOURCE_DIR})


option(ENABLE_INSTRUMENTATION "Enable internal log messages" OFF)
if(ENABLE_INSTRUMENTATION)
    set(INTELSEAPI_ROOT "" CACHE PATH "Root directory of IntelSEAPI")

    add_library(ittnotify STATIC IMPORTED)

    set_target_properties(ittnotify PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${INTELSEAPI_ROOT}/include"
        IMPORTED_LINK_INTERFACE_LIBRARIES "pthread;dl"
        IMPORTED_LOCATION "${INTELSEAPI_ROOT}/lib/libittnotify64.a"
        INTERFACE_COMPILE_DEFINITIONS ENABLE_INSTRUMENTATION
        )
endif()

add_library(elfxplore-core STATIC
    logger.cxx
    SymbolReference.cxx
    SymbolReferenceSet.cxx
    nm.cxx
    Database2.cxx
    utils.cxx
    query-utils.cxx
    command-utils.cxx
    database-utils.cxx
)
target_include_directories(elfxplore-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                          PRIVATE ${progschj_ThreadPool_DIR})
target_link_libraries(elfxplore-core
    PUBLIC SQLiteCpp Boost::system Boost::program_options Boost::filesystem
    PRIVATE SQLite::SQLite3 Threads::Threads OpenMP::OpenMP_CXX shellwords)
if (ENABLE_INSTRUMENTATION)
    target_link_libraries(elfxplore-core PRIVATE ittnotify)
endif()

add_executable(elfxplore
    progressbar.cxx
    Database3.cxx
    task.cxx
    tasks/db-task.cxx
    tasks/extract-task.cxx
    tasks/dependencies-task.cxx
    tasks/analyse-task.cxx
    tasks/artifacts-task.cxx
    elfxplore.cxx)
target_include_directories(elfxplore PRIVATE ${TERMCOLOR_INCLUDE_DIR}
                                             ${progschj_ThreadPool_DIR})
target_link_libraries(elfxplore
    PRIVATE elfxplore-core Boost::system Boost::program_options Boost::filesystem
            Threads::Threads SQLiteCpp OpenMP::OpenMP_CXX shellwords linemarkers)
if (ENABLE_INSTRUMENTATION)
    target_link_libraries(elfxplore PRIVATE ittnotify)
endif()

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
