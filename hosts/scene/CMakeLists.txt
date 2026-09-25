# The scene tool (ADR-0048, D94): authoring help for scene documents.
# Tooling only; no client or server closure contains it.
rawframe_host(NAME scene_tool OUTPUT rawframe-scene
    SOURCES src/main.cpp
    DEPS world_kest scene kest result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME scene_usage COMMAND rawframe_host_scene_tool)
    set_tests_properties(scene_usage PROPERTIES WILL_FAIL TRUE)
endif()
