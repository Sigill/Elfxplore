project(elfxplore-cli)

add_executable(elfxplore
    progressbar.cxx
    Database3.cxx
    task.cxx
    tasks/db-task.cxx
    tasks/import-command-task.cxx
    tasks/extract-task.cxx
    tasks/dependencies-task.cxx
    tasks/analyse-task.cxx
    tasks/artifacts-task.cxx
    elfxplore.cxx
)

target_include_directories(elfxplore
    PRIVATE
    ${jhasse_ThreadPool_DIR}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(elfxplore
    PRIVATE elfxplore-core Boost::system Boost::program_options
            Threads::Threads SQLiteCpp OpenMP::OpenMP_CXX shellwords linemarkers)
