# The browser's transport (ADR-0084, D170): network providers whose every
# operation is a call to the page's WebTransport. Built only for the web; the
# test runner stands a relay in for the page (tools/web_transport_relay.mjs).
rawframe_module(NAME network_web
    SOURCES src/participant.cpp src/web.cpp
    DEPS network composition execution diagnostics result base
)

rawframe_module_tests(NAME network_web SOURCES tests/web_test.cpp)
