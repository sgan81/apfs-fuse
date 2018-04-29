cmake_minimum_required(VERSION 2.5)
# set(CMAKE_C_COMPILER "clang")
# set(CMAKE_CXX_COMPILER "clang++")

project(Apfs)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -std=c++11 -Wall -Wextra -march=native")
set(CMAKE_VERBOSE_MAKEFILE ON)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

include_directories(. 3rdparty/lzfse/src)

add_subdirectory(3rdparty/lzfse)
add_subdirectory(ApfsLib)
add_subdirectory(ApfsDump)
add_subdirectory(ApfsDumpQuick)
add_subdirectory(apfsfuse)
