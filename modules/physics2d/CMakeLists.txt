# 2D physics in the World (ADR-0028, SPEC-0037) through Maul2D: the physics
# components, the step system, and the participant that adds them for a game
# with physics. Maul2D is linked privately, so no header above this module
# can include it.
rawframe_module(NAME physics2d
    SOURCES src/characters.cpp src/components.cpp src/participant.cpp src/physics.cpp
    DEPS collision world_runtime world composition schema execution diagnostics result base
)
target_link_libraries(rawframe_physics2d PRIVATE maul2d::maul2d)

rawframe_module_tests(NAME physics2d SOURCES tests/physics_test.cpp)
