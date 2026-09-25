# Resources and verified content (ADR-0013, SPEC-0008): typed identity,
# manifests, the catalog, and bytes read and verified before anyone sees
# them. Headless; no decoded assets, importers, or cookers here.
rawframe_module(NAME content
    SOURCES src/catalog.cpp src/identity.cpp src/manifest.cpp src/source.cpp src/store.cpp
    DEPS document execution result base
)

rawframe_module_tests(NAME content SOURCES tests/catalog_test.cpp tests/store_test.cpp)
