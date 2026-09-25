cmake_minimum_required(VERSION 3.10)
project(backtester CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories(include)

add_executable(backtest src/main.cpp)
add_executable(benchmark src/benchmark.cpp)
