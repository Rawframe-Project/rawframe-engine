# 3D physics in the World (ADR-0028, SPEC-0037) through Maul3D: the physics
# components, the step system, and the participant that adds them for a game
# with 3D physics. Maul3D is linked privately, so no header above this
# module can include it.
rawframe_module(NAME physics3d
    SOURCES src/components.cpp src/participant.cpp src/physics.cpp
    DEPS physics world_runtime world composition schema execution diagnostics result base
)
target_link_libraries(rawframe_physics3d PRIVATE maul3d::maul3d)

rawframe_module_tests(NAME physics3d SOURCES tests/physics_test.cpp)
