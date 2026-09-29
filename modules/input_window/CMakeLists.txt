# A window's raw input as a player's devices: the bridge from
# rawframe.window (SPEC-0025) to rawframe.input's feed (SPEC-0029). Neither
# of the two knows the other. Client only, and only where there are windows.
if(NOT TARGET rawframe_window)
    return()
endif()

rawframe_module(NAME input_window SOURCES src/bridge.cpp DEPS input window result base)

rawframe_module_tests(NAME input_window SOURCES tests/bridge_test.cpp)
