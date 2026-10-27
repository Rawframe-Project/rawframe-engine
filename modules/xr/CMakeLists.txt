# OpenXR's systems and sessions: the XR
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
# Monado's manifest, which the headset tests' configurations name as
# `xr.runtime`, and the line that names it.
find_file(RAWFRAME_MONADO_MANIFEST openxr_monado.json PATHS /usr/share/openxr/1 /usr/local/share/openxr/1 NO_DEFAULT_PATH)
if(RAWFRAME_MONADO_MANIFEST)
    set(RAWFRAME_TEST_XR_RUNTIME "xr.runtime = ${RAWFRAME_MONADO_MANIFEST}" CACHE INTERNAL "")
else()
    set(RAWFRAME_TEST_XR_RUNTIME "" CACHE INTERNAL "")
endif()
if(RAWFRAME_MONADO_SERVICE AND NOT WIN32 AND RAWFRAME_BUILD_TESTS)
    add_executable(rawframe_xr_tests tests/session_test.cpp)
    target_link_libraries(rawframe_xr_tests PRIVATE rawframe::xr rawframe::test)
    add_test(NAME xr COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xr_run.sh $<TARGET_FILE:rawframe_xr_tests>)
    # Hands tracked: the tracked-source test again, under Monado's
    # remote driver, whose right hand tests/hands.py says is tracked.
    add_test(NAME xr_tracked_hands
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xr_run.sh --hands ${CMAKE_CURRENT_SOURCE_DIR}/tests/hands.py
                     $<TARGET_FILE:rawframe_xr_tests> TrackedHands)
    set_tests_properties(xr_tracked_hands PROPERTIES ENVIRONMENT "RAWFRAME_XR_HANDS=1")
    # The runtime's leak, as every module test's (tools/libraries.lsan).
    if(RAWFRAME_SANITIZE MATCHES "address")
        set_tests_properties(xr PROPERTIES ENVIRONMENT
                             "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/libraries.lsan")
        set_tests_properties(xr_tracked_hands PROPERTIES ENVIRONMENT
                             "RAWFRAME_XR_HANDS=1;LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/libraries.lsan")
    endif()
else()
    rawframe_module_tests(NAME xr SOURCES tests/session_test.cpp)
endif()
