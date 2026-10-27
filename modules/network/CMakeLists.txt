# Provider-neutral networking: the generation-1
# application wire protocol first. Knows nothing of the World.
rawframe_module(NAME network
    SOURCES src/admission.cpp src/close.cpp src/conditions.cpp src/session.cpp src/wire.cpp
    DEPS composition execution diagnostics result base
)

rawframe_module_tests(NAME network SOURCES tests/admission_test.cpp tests/conditions_test.cpp tests/wire_test.cpp)
rawframe_module_fuzz(NAME network TARGET wire SOURCES tests/fuzz_wire.cpp)
