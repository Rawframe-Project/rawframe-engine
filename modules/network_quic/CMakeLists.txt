# The QUIC provider (SPEC-0010, ADR-0015): the provider contract over MsQuic
# with TLS 1.3 from OpenSSL. Both are linked privately, so no header above
# this module can include them.
rawframe_module(NAME network_quic
    SOURCES src/certificate.cpp src/participant.cpp src/quic.cpp
    DEPS network composition execution diagnostics result base
)
target_link_libraries(rawframe_network_quic PRIVATE msquic::msquic)

rawframe_module_tests(NAME network_quic SOURCES tests/quic_test.cpp)
