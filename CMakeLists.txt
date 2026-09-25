cmake_minimum_required(VERSION 3.28)

project(rawframe LANGUAGES C CXX)

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
add_subdirectory(third_party/kest)
add_subdirectory(third_party/maul2d)
add_subdirectory(third_party/maul3d)
add_subdirectory(third_party/miniaudio)
include(third_party/quic.cmake)
add_subdirectory(modules/base)
add_subdirectory(modules/result)
add_subdirectory(modules/diagnostics)
add_subdirectory(modules/execution)
add_subdirectory(modules/composition)
add_subdirectory(modules/document)
add_subdirectory(modules/audio)
add_subdirectory(modules/input)
add_subdirectory(modules/schema)
add_subdirectory(modules/world)
add_subdirectory(modules/world_snapshot)
add_subdirectory(modules/kest)
add_subdirectory(modules/world_runtime)
add_subdirectory(modules/network)
add_subdirectory(modules/network_loopback)
add_subdirectory(modules/network_quic)
add_subdirectory(modules/physics)
add_subdirectory(modules/physics2d)
add_subdirectory(modules/physics3d)
add_subdirectory(modules/world_replication)
add_subdirectory(modules/world_kest)
add_subdirectory(modules/input_kest)
add_subdirectory(modules/world_audio)
add_subdirectory(modules/host)

add_subdirectory(hosts/dedicated_server)
add_subdirectory(hosts/arena)
add_subdirectory(hosts/bots)

# Scenarios: several modules and hosts together, the way a game uses them.
if(RAWFRAME_BUILD_TESTS)
    add_subdirectory(tests/scenarios)
endif()
