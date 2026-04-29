cmake_minimum_required(VERSION 4.2)
project(BugzEx)
set(CMAKE_CXX_STANDARD 23)

include(cmake/dependencies.cmake)

add_executable(BugzEx src/main.cpp
        include/bugz/custom_lz.cpp
        include/bugz/custom_lz.hpp)
target_include_directories(BugzEx PUBLIC include)
target_link_libraries(BugzEx PUBLIC RedsCore)