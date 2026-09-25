# OpenSSL's libcrypto for the web build (cmake/wasm32-wasi.cmake), at the
# revision third_party/quic.cmake pins, for Ed25519 verification
# (SPEC-0019). Built once per machine into the dependency cache, like the
# native build, by tools/build_openssl_wasm.sh.
#
# Defines openssl::crypto, as third_party/quic.cmake does natively.

set(RAWFRAME_OPENSSL_REVISION "453eaaa9e6bb1304730abacfbb73d51868cb6ab9")
find_program(RAWFRAME_OPENSSL_WASM_COMPILER NAMES clang-20 REQUIRED)

if(DEFINED ENV{RAWFRAME_DEPENDENCY_CACHE})
    set(rawframe_openssl_cache "$ENV{RAWFRAME_DEPENDENCY_CACHE}")
elseif(DEFINED ENV{XDG_CACHE_HOME})
    set(rawframe_openssl_cache "$ENV{XDG_CACHE_HOME}/rawframe")
else()
    set(rawframe_openssl_cache "$ENV{HOME}/.cache/rawframe")
endif()
file(SHA256 "${PROJECT_SOURCE_DIR}/tools/build_openssl_wasm.sh" rawframe_openssl_script_hash)
file(SHA256 "${PROJECT_SOURCE_DIR}/cmake/openssl_wasi_shim.h" rawframe_openssl_shim_hash)
string(SHA256 rawframe_openssl_key
    "${RAWFRAME_OPENSSL_REVISION} ${rawframe_openssl_script_hash} ${rawframe_openssl_shim_hash} ${RAWFRAME_OPENSSL_WASM_COMPILER}")
string(SUBSTRING "${rawframe_openssl_key}" 0 16 rawframe_openssl_key)
set(RAWFRAME_OPENSSL_WASM_PREFIX "${rawframe_openssl_cache}/openssl-wasm-${rawframe_openssl_key}")

file(MAKE_DIRECTORY "${rawframe_openssl_cache}")
file(LOCK "${RAWFRAME_OPENSSL_WASM_PREFIX}.lock" GUARD PROCESS TIMEOUT 1200)
if(NOT EXISTS "${RAWFRAME_OPENSSL_WASM_PREFIX}/complete")
    message(STATUS "Building OpenSSL for WebAssembly into ${RAWFRAME_OPENSSL_WASM_PREFIX}")
    execute_process(
        COMMAND "${PROJECT_SOURCE_DIR}/tools/build_openssl_wasm.sh" "${RAWFRAME_OPENSSL_WASM_PREFIX}"
                "${RAWFRAME_OPENSSL_WASM_COMPILER}"
        RESULT_VARIABLE rawframe_openssl_result)
    if(NOT rawframe_openssl_result EQUAL 0)
        message(FATAL_ERROR "tools/build_openssl_wasm.sh failed")
    endif()
endif()
file(LOCK "${RAWFRAME_OPENSSL_WASM_PREFIX}.lock" RELEASE)

add_library(openssl::crypto STATIC IMPORTED GLOBAL)
set_target_properties(openssl::crypto PROPERTIES
    IMPORTED_LOCATION "${RAWFRAME_OPENSSL_WASM_PREFIX}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWFRAME_OPENSSL_WASM_PREFIX}/include"
    INTERFACE_LINK_LIBRARIES
        "wasi-emulated-signal;wasi-emulated-process-clocks;wasi-emulated-getpid;wasi-emulated-mman")
