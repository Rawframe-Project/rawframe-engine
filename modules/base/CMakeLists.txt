# The zero-dependency leaf of the module graph (ADR-0075): assertion and fatal
# primitives, the 128-bit value, the byte vocabulary, platform detection, and
# SHA-256 for fingerprints.
rawframe_module(NAME base SOURCES src/fatal.cpp src/sha256.cpp)

rawframe_module_tests(NAME base SOURCES
    tests/bits128_test.cpp
    tests/fatal_test.cpp
    tests/sha256_test.cpp
)
