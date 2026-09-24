# Diagnostics (SPEC-0004, SPEC-0047): event identity, the record, the router
# and its emitters, and the NDJSON sink with its bounded write path.
rawframe_module(NAME diagnostics SOURCES src/router.cpp src/emitter.cpp src/ndjson_sink.cpp DEPS base)

rawframe_module_tests(NAME diagnostics SOURCES
    tests/ndjson_test.cpp
    tests/router_test.cpp
)
