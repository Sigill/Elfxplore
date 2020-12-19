project(elfxplore-core)

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
    ansi.cxx
)

add_dependencies(elfxplore-core jhasse_ThreadPool)

target_include_directories(elfxplore-core
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(elfxplore-core
    PUBLIC SQLiteCpp Boost::system Boost::program_options ${FILESYSTEM_LIBRARY}
    PRIVATE SQLite::SQLite3 Threads::Threads OpenMP::OpenMP_CXX shellwords jhasse_ThreadPool)

target_link_instrmt(elfxplore-core)

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
