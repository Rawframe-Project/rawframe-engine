# The in-process network provider (SPEC-0010 loopback): providers reach each
# other by name, bytes are copied and owned, and delivery follows a clock and
# seeded latency, jitter, loss, and duplication.
rawframe_module(NAME network_loopback
    SOURCES src/loopback.cpp src/participant.cpp
    DEPS network composition execution diagnostics result base
)

rawframe_module_tests(NAME network_loopback SOURCES tests/loopback_test.cpp tests/session_test.cpp)
