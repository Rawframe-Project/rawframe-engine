cmake_minimum_required(VERSION 3.28)

project(rawframe LANGUAGES CXX)

# Strict C++23: no GNU extensions, and no C++20 module scanning (Rawframe uses
# headers, and scanning needs a clang-scan-deps that not every toolchain ships).
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(cmake/rawframe.cmake)

option(RAWFRAME_BUILD_TESTS "Build the tests" ON)

enable_testing()

if(RAWFRAME_BUILD_TESTS)
    add_subdirectory(tests/harness)
endif()

# Every module is listed here and in tools/modules.txt. A module is never
# discovered from the directory tree.
add_subdirectory(modules/base)
add_subdirectory(modules/result)
add_subdirectory(modules/diagnostics)
add_subdirectory(modules/execution)
add_subdirectory(modules/composition)
add_subdirectory(modules/schema)
add_subdirectory(modules/world)
