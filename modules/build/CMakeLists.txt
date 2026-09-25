# Builds (ADR-0023, ADR-0024, SPEC-0021): a cook's receipt-proven output
# packed into a Build, chunked, content-addressed, and named by its root
# hash. Tooling only: no runtime closure contains it.
rawframe_module(NAME build
    SOURCES src/build.cpp src/chunking.cpp src/compress.cpp
    DEPS content document result base
)
# Zstandard's encoder, for chunks worth compressing, linked privately.
target_link_libraries(rawframe_build PRIVATE rawframe_zstd_compress)

rawframe_module_tests(NAME build SOURCES tests/build_test.cpp tests/chunking_test.cpp)
