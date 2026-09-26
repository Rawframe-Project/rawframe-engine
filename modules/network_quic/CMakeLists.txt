# The QUIC provider (SPEC-0010, ADR-0015): the provider contract over MsQuic
# with TLS 1.3 from OpenSSL. Both are linked privately, so no header above
# this module can include them.
rawframe_module(NAME network_quic
    SOURCES src/certificate.cpp src/participant.cpp src/core.cpp src/qpack.cpp src/quic.cpp src/webtransport.cpp
    DEPS network composition execution diagnostics result base
)
target_link_libraries(rawframe_network_quic PRIVATE msquic::msquic)

rawframe_module_tests(NAME network_quic SOURCES tests/quic_test.cpp tests/webtransport_test.cpp)
if(TARGET rawframe_network_quic_tests)
    # The HTTP/3 and QPACK readers are this module's own; their tests read
    # them where they are.
    target_include_directories(rawframe_network_quic_tests PRIVATE src)
    # A browser's side, played by aioquic (python3-aioquic).
    target_compile_definitions(rawframe_network_quic_tests PRIVATE
        RAWFRAME_WEBTRANSPORT_CLIENT="${CMAKE_CURRENT_SOURCE_DIR}/tests/webtransport_client.py")
endif()
