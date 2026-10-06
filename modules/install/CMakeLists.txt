# Installing and updating Builds in a library (SPEC-0038): chunk-difference
# updates planned against what the store holds, fetched from any origin and
# verified, applied by one pointer write, rolled back, healed, and
# collected. Launcher and packaging tooling; never part of a client or a
# server, and never on the web, where a page fetches and holds its files.
rawframe_module(NAME install
    SOURCES src/files.cpp src/follow.cpp src/installation.cpp src/origin.cpp src/plan.cpp
    DEPS release content signature document result base
)

# Its tests pack the Builds they install, so they are a scenario
# (tests/scenarios/install_test.cpp).
