# Decoded assets: handles, readiness,
# coalesced requests, residency and budgets over verified content. Headless;
# never a render or presentation edge.
rawframe_module(NAME assets
    SOURCES src/assets.cpp
    DEPS content execution result base
)

rawframe_module_tests(NAME assets SOURCES tests/assets_test.cpp)
