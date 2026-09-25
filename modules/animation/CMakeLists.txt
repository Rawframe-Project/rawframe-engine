# Animation (ADR-0039, SPEC-0035): skeletons, clips, and graphs as canonical
# text documents, and their deterministic evaluation into poses and events. No
# renderer in it, so a server evaluates what gameplay declares relevant.
rawframe_module(NAME animation
    SOURCES src/blend_space.cpp src/clip.cpp src/mask.cpp src/graph.cpp src/instance.cpp src/modifiers.cpp src/phase_sync.cpp src/resources.cpp src/root_motion.cpp src/rotation.cpp src/sample.cpp src/skeleton.cpp src/stages.cpp
            src/state_machine.cpp src/text.cpp
    DEPS document result base
)

rawframe_module_tests(NAME animation SOURCES tests/additive_test.cpp tests/blend_space_test.cpp tests/clip_test.cpp tests/graph_test.cpp tests/instance_test.cpp tests/mask_test.cpp tests/modifier_test.cpp tests/phase_sync_test.cpp tests/property_test.cpp tests/root_motion_test.cpp tests/sample_test.cpp tests/skeleton_test.cpp tests/state_machine_test.cpp)
