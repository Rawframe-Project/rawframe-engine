# Provider-neutral networking (ADR-0015, SPEC-0010): the generation-1
# application wire protocol first. Knows nothing of the World.
rawframe_module(NAME network
    SOURCES src/wire.cpp
    DEPS result base
)

rawframe_module_tests(NAME network SOURCES tests/wire_test.cpp)
