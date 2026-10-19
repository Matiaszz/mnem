cmake_minimum_required(VERSION 4.3)
project(mnem)

set(CMAKE_CXX_STANDARD 14)

include_directories(src)

add_executable(mnem
        src/main.cpp
        src/main.h)
