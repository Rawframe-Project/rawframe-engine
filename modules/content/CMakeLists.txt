# Resources and verified content (ADR-0013, SPEC-0008): typed identity,
# manifests, the catalog, and bytes read and verified before anyone sees
# them. Headless; no decoded assets, importers, or cookers here.
rawframe_module(NAME content
    SOURCES src/catalog.cpp src/composition_record.cpp src/frame.cpp src/identity.cpp src/manifest.cpp src/product.cpp src/sidecar.cpp src/directory.cpp src/source.cpp src/store.cpp
    DEPS signature document execution result base
)
# Zstandard's decoder, for Build chunks (SPEC-0021), linked privately so no
# header above this module can include it.
target_link_libraries(rawframe_content PRIVATE rawframe_zstd_decompress)

rawframe_module_tests(NAME content SOURCES tests/catalog_test.cpp tests/store_test.cpp)
if(TARGET rawframe_content_tests)
    # The tests make Zstandard frames, good and bad, for a Build to hold,
    # and sign the Builds they make.
    target_link_libraries(rawframe_content_tests PRIVATE rawframe_zstd_compress openssl::crypto)
endif()
