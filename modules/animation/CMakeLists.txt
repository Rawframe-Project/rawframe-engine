# Animation (ADR-0039, SPEC-0035): skeletons and clips as canonical text
# documents, and their deterministic evaluation into poses and events. No
# renderer in it, so a server evaluates what gameplay declares relevant.
rawframe_module(NAME animation
    SOURCES src/clip.cpp src/graph.cpp src/instance.cpp src/rotation.cpp src/sample.cpp src/skeleton.cpp src/text.cpp
    DEPS document result base
)

rawframe_module_tests(NAME animation SOURCES tests/clip_test.cpp tests/graph_test.cpp tests/instance_test.cpp tests/sample_test.cpp tests/skeleton_test.cpp)
