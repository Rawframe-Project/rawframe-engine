# Publisher signatures (SPEC-0019, SPEC-0021, SPEC-0023): Ed25519
# verification through OpenSSL's EVP one-shot interface, the detached
# signature envelope, and publisher key sets with their fail-closed
# verdicts. Verification only: signing is packaging tooling's. OpenSSL is
# linked privately, so no header above this module can include it.
rawframe_module(NAME signature
    SOURCES src/signature.cpp
    DEPS document result base
)
target_link_libraries(rawframe_signature PRIVATE openssl::crypto)

rawframe_module_tests(NAME signature SOURCES tests/signature_test.cpp)
rawframe_module_fuzz(NAME signature TARGET records SEEDS tests/seeds SOURCES tests/fuzz_signature.cpp)
if(TARGET rawframe_signature_tests)
    # The tests sign, to have signatures to verify.
    target_link_libraries(rawframe_signature_tests PRIVATE openssl::crypto)
endif()
