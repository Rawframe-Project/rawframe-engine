# The 3D scene's GPU half (SPEC-0024's scene, D284) over the one device,
# apart from the CPU half because Maul RHI is not on every client yet.
# Client only: the dedicated server links none of it, and a platform Maul
# RHI is not built for has no module at all.
if(NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME render_scene_gpu SOURCES src/blocks.cpp src/meshes.cpp src/metering.cpp src/participant.cpp src/pipelines.cpp src/renderer.cpp src/tables.cpp src/temporal.cpp DEPS render render_scene material texture mesh composition diagnostics result base)

rawframe_module_tests(NAME render_scene_gpu SOURCES tests/renderer_test.cpp)
