# Maul RHI, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged: the one device layer (ADR-0029).
# On Linux and Android its Vulkan driver is loaded at run time from the
# system's Vulkan loader, so nothing links a GPU library; on Android it draws
# to the activity's native window (D550). On Windows its Direct3D 12
# driver, the engine's shader containers carrying each entry's DXIL (D415).
# On macOS and iOS its Metal driver, the engine's shader containers carrying
# each entry in Metal's language (D406, D586). On the web its WebGPU driver's JavaScript is imports the page gives
# from the maul-rhi.mjs this build writes (mrhi-0016, D282). Only a client
# that draws builds it: never the dedicated server's closure. Its test
# driver, which renders nothing and answers as a test describes, is built
# only where the engine's tests are.
set(RAWFRAME_MAUL_RHI OFF)
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|WASI|Android|iOS)$" OR WIN32)
    set(RAWFRAME_MAUL_RHI ON)
endif()
if(NOT RAWFRAME_MAUL_RHI)
    return()
endif()

set(MAUL_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(MAUL_RHI_INSTALL OFF CACHE BOOL "" FORCE)
# The one driver a build takes, and with it the shader containers it embeds
# (RAWFRAME_SHADER_SUFFIX, read by rawframe_shader_containers, D416): Vulkan
# and WebGPU read the same.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(MAUL_RHI_VULKAN_DRIVER OFF CACHE BOOL "" FORCE)
    set(MAUL_RHI_WEBGPU_DRIVER ON CACHE BOOL "" FORCE)
    set(RAWFRAME_SHADER_SUFFIX "")
elseif(APPLE)
    set(MAUL_RHI_VULKAN_DRIVER OFF CACHE BOOL "" FORCE)
    set(MAUL_RHI_METAL_DRIVER ON CACHE BOOL "" FORCE)
    set(RAWFRAME_SHADER_SUFFIX .metal)
elseif(WIN32)
    set(MAUL_RHI_VULKAN_DRIVER OFF CACHE BOOL "" FORCE)
    set(MAUL_RHI_D3D12_DRIVER ON CACHE BOOL "" FORCE)
    set(RAWFRAME_SHADER_SUFFIX .d3d12)
else()
    set(MAUL_RHI_VULKAN_DRIVER ON CACHE BOOL "" FORCE)
    set(MAUL_RHI_D3D12_DRIVER OFF CACHE BOOL "" FORCE)
    set(RAWFRAME_SHADER_SUFFIX "")
endif()
set(MAUL_RHI_TEST_DRIVER ${RAWFRAME_BUILD_TESTS} CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-rhi" "${CMAKE_BINARY_DIR}/third_party/maul-rhi" EXCLUDE_FROM_ALL)
