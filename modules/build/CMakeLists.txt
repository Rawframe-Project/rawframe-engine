# Builds (ADR-0023, ADR-0024, SPEC-0021): a cook's receipt-proven output
# packed into a Build, chunked, content-addressed, and named by its root
# hash. Tooling only: no runtime closure contains it.
rawframe_module(NAME build
    SOURCES src/build.cpp src/chunking.cpp src/compress.cpp src/publisher_key.cpp
    DEPS signature content document result base
)
# Zstandard's encoder, for chunks worth compressing, and OpenSSL, for
# signing, linked privately.
target_link_libraries(rawframe_build PRIVATE rawframe_zstd_compress openssl::crypto)

rawframe_module_tests(NAME build SOURCES tests/build_test.cpp tests/chunking_test.cpp tests/golden_test.cpp)
if(TARGET rawframe_build_tests)
    target_compile_definitions(rawframe_build_tests PRIVATE RAWFRAME_BUILD_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/")
endif()
