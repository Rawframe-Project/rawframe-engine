# A Host driven from a window's frames, with the window's raw input lent
# as the player's devices (D250): what a desktop client and a page run.
# Only where there are windows.
if(NOT TARGET rawframe_input_window)
    return()
endif()

rawframe_module(NAME window_host SOURCES src/window_host.cpp src/asides.cpp src/icon.cpp
    DEPS host world_tooling view input_kest input_window input window ui texture composition execution result base)
