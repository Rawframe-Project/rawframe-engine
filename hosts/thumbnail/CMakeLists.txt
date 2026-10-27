# The thumbnail tool: a cooked mesh drawn offscreen by the scene's
# renderer and kept as a texture an asset browser shows (Tool role). Only
# where Maul RHI is built.
if(NOT TARGET rawframe_render_scene_gpu)
    return()
endif()

rawframe_host(NAME thumbnail_tool OUTPUT rawframe-thumbnail
    SOURCES src/main.cpp
    DEPS render_scene_gpu render_scene render material content mesh texture schema result base
)

if(RAWFRAME_BUILD_TESTS)
    add_test(NAME thumbnail_usage COMMAND rawframe_host_thumbnail_tool)
    set_tests_properties(thumbnail_usage PROPERTIES WILL_FAIL TRUE)
endif()
