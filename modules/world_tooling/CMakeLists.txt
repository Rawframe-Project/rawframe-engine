# The tooling protocol's runtime end (ADR-0032, D408): an endpoint a
# composition selects, on a provider of its own, through which authoring
# clients, test drivers, and agents inspect a running Runtime.
rawframe_module(NAME world_tooling
    SOURCES src/participant.cpp src/server.cpp src/window_input.cpp
    DEPS world_runtime network world composition schema execution diagnostics document result base
)

rawframe_module_tests(NAME world_tooling SOURCES tests/server_test.cpp tests/window_input_test.cpp)
rawframe_module_fuzz(NAME world_tooling TARGET records RUNS 15000 MAX_LEN 4096 SEEDS tests/seeds SOURCES tests/fuzz_tooling.cpp)
