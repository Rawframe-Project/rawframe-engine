# The zero-dependency leaf of the module graph (ADR-0075): assertion and fatal
# primitives, the 128-bit value, the byte vocabulary, platform detection,
# SHA-256 for fingerprints, and the operating system's secure random source.
rawframe_module(NAME base SOURCES src/fatal.cpp src/secure_random.cpp src/sha256.cpp)

rawframe_module_tests(NAME base SOURCES
    tests/bits128_test.cpp
    tests/fatal_test.cpp
    tests/secure_random_test.cpp
    tests/sha256_test.cpp
)

# SHA-256 is optimized in every configuration: the cook hashes its own
# executable and every source for its keys, and unoptimized hashing under
# the sanitizers took a recook past the runners_recooked test's window
# (D146). Its known-answer tests hold it to the standard either way.
if(NOT MSVC)
    set_source_files_properties(src/sha256.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
