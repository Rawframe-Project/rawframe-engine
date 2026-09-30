# Windows and raw input (SPEC-0025) over Maul Window, private to this
# module. Client only: the dedicated server links none of it, and a
# platform Maul Window does not serve yet has no window module at all.
if(NOT TARGET maul-window)
    return()
endif()

set(rawframe_window_sources src/records.cpp src/surfaces.cpp src/windows.cpp)
if(RAWFRAME_BUILD_TESTS)
    # The headless test platform exists only where the tests do.
    list(APPEND rawframe_window_sources src/testing.cpp)
endif()
rawframe_module(NAME window SOURCES ${rawframe_window_sources} DEPS composition result base)
target_link_libraries(rawframe_window PRIVATE maul-window)

rawframe_module_tests(NAME window SOURCES tests/windows_test.cpp)
