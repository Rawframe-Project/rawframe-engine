# The build tool (ADR-0024, SPEC-0021): packaging's process entry over
# rawframe.build. No runtime closure contains it.
rawframe_host(NAME build_tool OUTPUT rawframe-build
    SOURCES src/main.cpp
    DEPS build content document result base
)
target_compile_definitions(rawframe_host_build_tool PRIVATE RAWFRAME_ENGINE_VERSION="${PROJECT_VERSION}")

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME build_usage COMMAND rawframe_host_build_tool)
    set_tests_properties(build_usage PROPERTIES WILL_FAIL TRUE)
endif()
