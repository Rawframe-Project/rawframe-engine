# Animation in the World (ADR-0039, SPEC-0035): the Animator component, the
# step system that plays each entity's graph from its game's parameters,
# and the participant that adds it for a game with animators. No renderer
# in it, so a dedicated server plays what gameplay declares relevant.
rawframe_module(NAME world_animation
    SOURCES src/animation.cpp src/components.cpp src/participant.cpp
    DEPS animation world_runtime world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME world_animation SOURCES tests/animation_test.cpp)
