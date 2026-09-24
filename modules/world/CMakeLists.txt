# World (ADR-0010, ADR-0011, SPEC-0005, SPEC-0006): entity identity, archetype
# storage, queries, command buffers, and the simulation schedule.
rawframe_module(NAME world
    SOURCES
        src/archetype.cpp
        src/command_buffer.cpp
        src/world.cpp
    DEPS schema execution diagnostics result base
)

rawframe_module_tests(NAME world SOURCES
    tests/command_buffer_test.cpp
    tests/query_test.cpp
    tests/world_test.cpp
)
