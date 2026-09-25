# The cook tool (ADR-0024): import tooling's process entry over rawframe.cook.
# No runtime closure contains it.
rawframe_host(NAME cook_tool OUTPUT rawframe-cook
    SOURCES src/main.cpp
    DEPS cook scene world_kest input kest_library kest audio_import audio content document execution result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME cook_usage COMMAND rawframe_host_cook_tool)
    set_tests_properties(cook_usage PROPERTIES WILL_FAIL TRUE)
endif()
