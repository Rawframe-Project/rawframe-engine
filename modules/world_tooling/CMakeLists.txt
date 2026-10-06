# The tooling protocol's runtime end (ADR-0032, D408): an endpoint a
# composition selects, on a provider of its own, through which authoring
# clients, test drivers, and agents inspect a running Runtime.
rawframe_module(NAME world_tooling
    SOURCES src/participant.cpp src/server.cpp
    DEPS world_runtime network world composition schema execution diagnostics document result base
)

rawframe_module_tests(NAME world_tooling SOURCES tests/server_test.cpp)
