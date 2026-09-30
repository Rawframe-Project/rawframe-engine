# The one device (ADR-0045's rawframe.render, SPEC-0024) over Maul RHI,
# which only the rendering cluster declares. Client only: the dedicated
# server links none of it, and a platform Maul RHI is not built for has no
# render module at all.
if(NOT TARGET maul-rhi)
    return()
endif()

rawframe_module(NAME render SOURCES src/capture.cpp src/device.cpp src/display.cpp src/frame.cpp src/frames.cpp src/participant.cpp DEPS window composition diagnostics result base)
target_link_libraries(rawframe_render PUBLIC maul-rhi)

rawframe_module_tests(NAME render SOURCES tests/device_test.cpp tests/frame_test.cpp)
