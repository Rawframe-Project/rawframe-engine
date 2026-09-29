# The canvas's GPU half (SPEC-0024's canvas, D278) over the one device, apart
# from the CPU half because Maul RHI is not on every client yet. Client
# only: the dedicated server links none of it, and a platform Maul RHI is
# not built for has no module at all.
if(NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME render_canvas_gpu SOURCES src/renderer.cpp DEPS render render_canvas texture result base)

rawframe_module_tests(NAME render_canvas_gpu SOURCES tests/renderer_test.cpp)
