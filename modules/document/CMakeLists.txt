# Authored documents: SPEC-0028's canonicalization profile, JSON read
# strictly and written one way.
rawframe_module(NAME document
    SOURCES src/json.cpp
    DEPS result base
)

rawframe_module_tests(NAME document SOURCES tests/json_test.cpp)
