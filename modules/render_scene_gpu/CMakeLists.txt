# The 3D scene's GPU half (SPEC-0024's scene, D284) over the one device,
# apart from the CPU half because Maul RHI is not on every client yet.
# Client only: the dedicated server links none of it, and a platform Maul
# RHI is not built for has no module at all.
if(NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME render_scene_gpu SOURCES src/blocks.cpp src/bloom.cpp src/capture.cpp src/casting.cpp src/contact.cpp src/decals.cpp src/probes.cpp src/environment.cpp src/focus.cpp src/meshes.cpp src/metering.cpp src/motion.cpp src/occlusion.cpp src/participant.cpp src/picture.cpp src/pipelines.cpp src/reflection.cpp src/renderer.cpp src/runs.cpp src/tables.cpp src/temporal.cpp DEPS render render_scene material texture mesh composition diagnostics result base)

rawframe_module_tests(NAME render_scene_gpu SOURCES tests/bloom_test.cpp tests/capture_test.cpp tests/contact_test.cpp tests/decal_test.cpp tests/environment_test.cpp tests/focus_test.cpp tests/grading_test.cpp tests/motion_test.cpp tests/multisample_test.cpp tests/occlusion_test.cpp tests/reflection_test.cpp tests/renderer_test.cpp tests/shadow_test.cpp)
