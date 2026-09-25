# Semantic input (ADR-0037, SPEC-0029): action sets, and the mapping from
# device controls to per-player action state. Client only: the dedicated
# server links none of it.
rawframe_module(NAME input
    SOURCES src/controls.cpp src/actions.cpp
    DEPS document result base
)

rawframe_module_tests(NAME input SOURCES tests/actions_test.cpp)
