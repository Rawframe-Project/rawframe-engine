# Provider-neutral networking (ADR-0015, SPEC-0010): the generation-1
# application wire protocol first. Knows nothing of the World.
rawframe_module(NAME network
    SOURCES src/admission.cpp src/close.cpp src/session.cpp src/wire.cpp
    DEPS composition execution diagnostics result base
)

rawframe_module_tests(NAME network SOURCES tests/admission_test.cpp tests/wire_test.cpp)
rawframe_module_fuzz(NAME network TARGET wire SOURCES tests/fuzz_wire.cpp)
