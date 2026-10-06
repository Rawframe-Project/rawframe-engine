# The launcher's HTTP client (D414): GET over HTTP/1.1, plain or TLS through
# OpenSSL, verifying every certificate for its host. A launcher unit, outside
# every engine closure (ADR-0060, ADR-0074): no module or host depends on it.
rawframe_module(NAME http
    SOURCES src/client.cpp src/connection.cpp src/response.cpp src/url.cpp
    DEPS result base
)
target_link_libraries(rawframe_http PRIVATE openssl::ssl)
if(WIN32)
    target_link_libraries(rawframe_http PRIVATE ws2_32 crypt32)
endif()

rawframe_module_tests(NAME http SOURCES tests/client_test.cpp tests/response_test.cpp tests/url_test.cpp)
rawframe_module_fuzz(NAME http TARGET response RUNS 50000 SEEDS tests/seeds SOURCES tests/fuzz_response.cpp)
