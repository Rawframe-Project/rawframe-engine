# Maul RHI, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged: the one device layer (ADR-0029),
# its Vulkan driver loaded at run time from the system's Vulkan loader, so
# nothing links a GPU library. Only a client that draws builds it: never the
# dedicated server's closure, and not yet on the web, whose WebGPU driver
# wants a browser build the engine's client does not make yet (D277). Its
# test driver, which renders nothing and answers as a test describes, is
# built only where the engine's tests are.
set(RAWFRAME_MAUL_RHI OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
    set(RAWFRAME_MAUL_RHI ON)
endif()
if(NOT RAWFRAME_MAUL_RHI)
    return()
endif()

set(MAUL_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_INSTALL OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_VULKAN_DRIVER ON CACHE BOOL "" FORCE)
set(MAUL_RHI_TEST_DRIVER ${RAWFRAME_BUILD_TESTS} CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-rhi" "${CMAKE_BINARY_DIR}/third_party/maul-rhi" EXCLUDE_FROM_ALL)
