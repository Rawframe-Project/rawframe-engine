# A game's cooked content in a process (ADR-0013, ADR-0024, SPEC-0008): one
# content store over the cook's output, shared through composition by every
# asset family, and a changed manifest published as the next catalog.
rawframe_module(NAME game_content
    SOURCES src/cooked_content.cpp src/participant.cpp
    DEPS signature content composition document execution diagnostics result base
)

rawframe_module_tests(NAME game_content SOURCES tests/cooked_content_test.cpp)
