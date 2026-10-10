# OpenXR's systems and sessions (ADR-0081's XR admission, D591): the XR
# module, a presentation producer beside the window module, over Khronos's
# loader. Client only: the dedicated server links none of it. Built where
# the loader is (Linux and Windows) and the render module is.
if(NOT TARGET rawframe_openxr_loader OR NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME xr SOURCES src/participant.cpp src/runtime.cpp src/session.cpp DEPS render view input_kest input composition diagnostics execution result base)
target_link_libraries(rawframe_xr PRIVATE rawframe_openxr_loader)
# Vulkan's headers as Maul RHI vendors them, for the runtime's binding.
target_include_directories(rawframe_xr SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/maul-rhi/khronos)

# Under a runtime of its own where Monado is installed (tools/xr_run.sh);
# where none is, the tests find no runtime and skip.
find_program(RAWFRAME_MONADO_SERVICE monado-service)
if(RAWFRAME_MONADO_SERVICE AND NOT WIN32 AND RAWFRAME_BUILD_TESTS)
    add_executable(rawframe_xr_tests tests/session_test.cpp)
    target_link_libraries(rawframe_xr_tests PRIVATE rawframe::xr rawframe::test)
    add_test(NAME xr COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xr_run.sh $<TARGET_FILE:rawframe_xr_tests>)
else()
    rawframe_module_tests(NAME xr SOURCES tests/session_test.cpp)
endif()
