# The OpenXR loader and headers (ADR-0081's XR admission, D591), vendored
# from Khronos's OpenXR-SDK at the release recorded in third_party/README.md.
# This file is ours; everything else in this directory is upstream's own,
# unchanged. Built static, without exception handling (the loader's and
# jsoncpp's own switches, as the engine builds without exceptions), on the
# desktop platforms OpenXR runtimes serve: Linux and Windows. Only the XR
# module links it, and no dedicated server reaches that.
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32))
    return()
endif()

set(rawframe_openxr ${CMAKE_CURRENT_SOURCE_DIR})
add_library(rawframe_openxr_loader STATIC
    ${rawframe_openxr}/src/loader/api_layer_interface.cpp
    ${rawframe_openxr}/src/loader/loader_core.cpp
    ${rawframe_openxr}/src/loader/loader_init_data.cpp
    ${rawframe_openxr}/src/loader/loader_instance.cpp
    ${rawframe_openxr}/src/loader/loader_logger.cpp
    ${rawframe_openxr}/src/loader/loader_logger_recorders.cpp
    ${rawframe_openxr}/src/loader/loader_properties.cpp
    ${rawframe_openxr}/src/loader/manifest_file.cpp
    ${rawframe_openxr}/src/loader/runtime_interface.cpp
    ${rawframe_openxr}/src/loader/xr_generated_loader.cpp
    ${rawframe_openxr}/src/common/filesystem_utils.cpp
    ${rawframe_openxr}/src/common/object_info.cpp
    ${rawframe_openxr}/src/xr_generated_dispatch_table_core.c
    ${rawframe_openxr}/src/external/jsoncpp/src/lib_json/json_reader.cpp
    ${rawframe_openxr}/src/external/jsoncpp/src/lib_json/json_value.cpp
    ${rawframe_openxr}/src/external/jsoncpp/src/lib_json/json_writer.cpp
)
target_include_directories(rawframe_openxr_loader SYSTEM PUBLIC ${rawframe_openxr}/include)
target_include_directories(rawframe_openxr_loader PRIVATE
    ${rawframe_openxr}/src ${rawframe_openxr}/src/common ${rawframe_openxr}/src/loader
    ${rawframe_openxr}/src/external/jsoncpp/include)
target_compile_definitions(rawframe_openxr_loader PRIVATE XRLOADER_DISABLE_EXCEPTION_HANDLING JSON_USE_EXCEPTION=0)
# C++17, as upstream builds it: clang-cl's default is C++14, which has no
# std::scoped_lock.
target_compile_features(rawframe_openxr_loader PRIVATE cxx_std_17)
if(WIN32)
    # Win32 as upstream builds it: its utilities read windows.h through
    # the platform switch.
    target_compile_definitions(rawframe_openxr_loader PRIVATE XR_OS_WINDOWS XR_USE_PLATFORM_WIN32 NOMINMAX)
    target_link_libraries(rawframe_openxr_loader PRIVATE advapi32)
else()
    # Where runtimes and layers are found when XDG's directories are unset,
    # as upstream's build defaults them; and the secure getenv glibc has.
    target_compile_definitions(rawframe_openxr_loader PRIVATE XR_OS_LINUX HAVE_SECURE_GETENV
        FALLBACK_CONFIG_DIRS="/etc/xdg" FALLBACK_DATA_DIRS="/usr/local/share:/usr/share" SYSCONFDIR="/etc")
    find_package(Threads REQUIRED)
    target_link_libraries(rawframe_openxr_loader PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
endif()
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_openxr_loader PRIVATE -w)
set_target_properties(rawframe_openxr_loader PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                                        C_VISIBILITY_PRESET hidden)
