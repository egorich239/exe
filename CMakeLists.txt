cmake_minimum_required(VERSION 3.20)
project(exe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

add_executable(exe main.cpp exe.h)
