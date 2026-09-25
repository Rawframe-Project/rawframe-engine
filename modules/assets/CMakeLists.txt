# Decoded assets (ADR-0025, ADR-0045, SPEC-0027): handles, readiness,
# coalesced requests, residency and budgets over verified content. Headless;
# never a render or presentation edge.
rawframe_module(NAME assets
    SOURCES src/assets.cpp
    DEPS content execution result base
)

rawframe_module_tests(NAME assets SOURCES tests/assets_test.cpp)
