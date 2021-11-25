cmake_minimum_required(VERSION 3.18)
project(exe)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -S")

add_library(exe_lib INTERFACE)
target_include_directories(exe_lib INTERFACE .)

add_executable(exe main.cpp)
target_link_libraries(exe PRIVATE exe_lib)

add_library(bench_sync bench/sync.cpp)
target_link_libraries(bench_sync PRIVATE exe_lib)
