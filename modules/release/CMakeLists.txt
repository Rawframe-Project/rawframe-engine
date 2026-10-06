# SPEC-0020's release records and channel pointers, and the update check a
# launcher makes over them (D424). Launcher and release tooling; never part
# of a client or a server.
rawframe_module(NAME release
    SOURCES src/release.cpp
    DEPS content document base result
)
rawframe_module_tests(NAME release SOURCES tests/release_test.cpp)
rawframe_module_fuzz(NAME release TARGET records RUNS 10000 SEEDS tests/seeds/records SOURCES tests/fuzz_records.cpp)
