# The QUIC provider (SPEC-0010, ADR-0015): the provider contract over MsQuic
# with TLS 1.3 from OpenSSL. Both are linked privately, so no header above
# this module can include them.
rawframe_module(NAME network_quic
    SOURCES src/allocations.cpp src/certificate.cpp src/participant.cpp src/core.cpp src/qpack.cpp src/quic.cpp
            src/webtransport.cpp
    DEPS network composition execution diagnostics result base
)
target_link_libraries(rawframe_network_quic PRIVATE msquic::msquic)
# MsQuic's heap, counted where the linker routes its allocations (D234): a
# program that links this module links through the counters. The counters
# are asked for up front, so their object is taken from this archive even
# when nothing else of it names them, before MsQuic's archive needs them.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(rawframe_network_quic PRIVATE RAWFRAME_QUIC_HEAP_COUNTED=1)
    target_link_options(rawframe_network_quic INTERFACE "LINKER:--undefined=__wrap_CxPlatAlloc"
                        "LINKER:--wrap=CxPlatAlloc" "LINKER:--wrap=CxPlatAllocUninitialized"
                        "LINKER:--wrap=CxPlatFree")
endif()

rawframe_module_tests(NAME network_quic SOURCES tests/quic_test.cpp tests/webtransport_test.cpp)
if(TARGET rawframe_network_quic_tests)
    # The HTTP/3 and QPACK readers are this module's own; their tests read
    # them where they are.
    target_include_directories(rawframe_network_quic_tests PRIVATE src)
    # A browser's side, played by aioquic (python3-aioquic), under the
    # Python this machine names (python3 is not a name on Windows, D237).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # Forward slashes, which a C string reads as they are.
    file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" rawframe_python)
    target_compile_definitions(rawframe_network_quic_tests PRIVATE
        RAWFRAME_PYTHON="${rawframe_python}"
        RAWFRAME_WEBTRANSPORT_CLIENT="${CMAKE_CURRENT_SOURCE_DIR}/tests/webtransport_client.py")
endif()
rawframe_module_fuzz(NAME network_quic TARGET webtransport SOURCES tests/fuzz_webtransport.cpp)
if(TARGET rawframe_network_quic_fuzz_webtransport)
    target_include_directories(rawframe_network_quic_fuzz_webtransport PRIVATE src)
endif()
