# The zero-dependency leaf of the module graph (ADR-0075): assertion and fatal
# primitives, the 128-bit value, the byte vocabulary, platform detection.
rawframe_module(NAME base SOURCES src/fatal.cpp)

rawframe_module_tests(NAME base SOURCES
    tests/bits128_test.cpp
    tests/fatal_test.cpp
)
