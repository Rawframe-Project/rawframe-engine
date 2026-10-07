# The cook tool (ADR-0024): import tooling's process entry over rawframe.cook.
# No runtime closure contains it.
rawframe_host(NAME cook_tool OUTPUT rawframe-cook
    SOURCES src/main.cpp
    DEPS cook process localization animation material graph mesh_import mesh scene world_kest input kest_library kest audio_import audio content document execution result base
)
# The engine's shader generator, which builds a program material (D484).
target_compile_definitions(rawframe_host_cook_tool PRIVATE
    RAWFRAME_SHADER_GENERATOR="${PROJECT_SOURCE_DIR}/tools/gen_shaders.py")

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME cook_usage COMMAND rawframe_host_cook_tool)
    set_tests_properties(cook_usage PROPERTIES WILL_FAIL TRUE)
    # An author's crate with no subasset map, mapped by the tool and cooked.
    add_test(NAME cook_map_subassets
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/map_subassets.sh
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/map_subassets)
endif()
