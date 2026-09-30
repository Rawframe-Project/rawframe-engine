# Reflection probes baked (D326): the scene drawn six ways from each
# probe's middle on the one device, its light read back, and written as the
# Radiance picture the cook makes its environment of. Toolchain only: no
# process that plays reaches it, and where Maul RHI is not built there is
# nothing to draw with.
if(NOT TARGET rawframe_render_scene_gpu)
    return()
endif()

rawframe_module(NAME scene_bake SOURCES src/bake.cpp src/participant.cpp DEPS render_scene_gpu render_scene texture_import texture world_kest graph composition diagnostics result base)

rawframe_module_tests(NAME scene_bake SOURCES tests/bake_test.cpp)
