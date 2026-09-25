# Authored scenes (ADR-0048, D92): a world's starting entities as a
# canonical text document, read and written in one form. Engine-neutral:
# the game checks components and fields when it spawns one.
rawframe_module(NAME scene
    SOURCES src/scene.cpp
    DEPS schema document result base
)

rawframe_module_tests(NAME scene SOURCES tests/scene_test.cpp)
