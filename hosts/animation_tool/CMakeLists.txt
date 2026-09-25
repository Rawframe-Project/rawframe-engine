# The animation tool (ADR-0039, SPEC-0035): a glTF skin and its animations
# made into skeleton and clip documents. Tooling only; no client or server
# closure contains it.
rawframe_host(NAME animation_tool OUTPUT rawframe-animation
    SOURCES src/main.cpp
    DEPS animation_import animation content result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME animation_usage COMMAND rawframe_host_animation_tool)
    set_tests_properties(animation_usage PROPERTIES WILL_FAIL TRUE)
endif()

if(RAWFRAME_BUILD_TESTS)
    # An arm imported from glTF, again with its identities kept, and cooked.
    add_test(NAME animation_from_gltf
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/from_gltf.sh $<TARGET_FILE:rawframe_host_animation_tool>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${CMAKE_CURRENT_SOURCE_DIR}/tests/arm.gltf
                     ${CMAKE_CURRENT_BINARY_DIR}/from_gltf)
    set_tests_properties(animation_from_gltf PROPERTIES
                         PASS_REGULAR_EXPRESSION "failed 0.*animation imported and cooked")
endif()
