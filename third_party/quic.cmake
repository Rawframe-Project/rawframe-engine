# MsQuic with OpenSSL, vendored at the revisions recorded in
# third_party/README.md. This file is ours; third_party/msquic and
# third_party/openssl are upstream's own, pruned and otherwise unchanged.
#
# OpenSSL takes longer to build than the engine, and the result is the same
# for every build tree, so tools/build_quic.sh builds both once per machine
# into a cache directory named by the revisions, the build script, and the
# compiler. Configure builds it when it is missing; a lock keeps six presets
# configuring at once from building it six times.
#
# Defines msquic::msquic, a static library with OpenSSL inside it, whose
# include directories hold both libraries' headers.

set(RAWFRAME_MSQUIC_REVISION "a01333cf7c2659cce0ff03ef3f21e1ff15bb5b83")
set(RAWFRAME_OPENSSL_REVISION "453eaaa9e6bb1304730abacfbb73d51868cb6ab9")

# One compiler for the dependency whatever the preset uses: it is C behind a
# C interface, and its warnings are chosen for Clang.
find_program(RAWFRAME_QUIC_C_COMPILER NAMES clang-20 clang cc REQUIRED)
find_program(RAWFRAME_QUIC_CXX_COMPILER NAMES clang++-20 clang++ c++ REQUIRED)

if(DEFINED ENV{RAWFRAME_DEPENDENCY_CACHE})
    set(rawframe_quic_cache "$ENV{RAWFRAME_DEPENDENCY_CACHE}")
elseif(DEFINED ENV{XDG_CACHE_HOME})
    set(rawframe_quic_cache "$ENV{XDG_CACHE_HOME}/rawframe")
else()
    set(rawframe_quic_cache "$ENV{HOME}/.cache/rawframe")
endif()
file(SHA256 "${PROJECT_SOURCE_DIR}/tools/build_quic.sh" rawframe_quic_script_hash)
string(SHA256 rawframe_quic_key
    "${RAWFRAME_MSQUIC_REVISION} ${RAWFRAME_OPENSSL_REVISION} ${rawframe_quic_script_hash} ${RAWFRAME_QUIC_C_COMPILER}")
string(SUBSTRING "${rawframe_quic_key}" 0 16 rawframe_quic_key)
set(RAWFRAME_QUIC_PREFIX "${rawframe_quic_cache}/quic-${rawframe_quic_key}")

file(MAKE_DIRECTORY "${rawframe_quic_cache}")
file(LOCK "${RAWFRAME_QUIC_PREFIX}.lock" GUARD PROCESS TIMEOUT 1200)
if(NOT EXISTS "${RAWFRAME_QUIC_PREFIX}/complete")
    message(STATUS "Building MsQuic and OpenSSL into ${RAWFRAME_QUIC_PREFIX}")
    execute_process(
        COMMAND "${PROJECT_SOURCE_DIR}/tools/build_quic.sh" "${RAWFRAME_QUIC_PREFIX}"
                "${RAWFRAME_QUIC_C_COMPILER}" "${RAWFRAME_QUIC_CXX_COMPILER}"
        RESULT_VARIABLE rawframe_quic_result)
    if(NOT rawframe_quic_result EQUAL 0)
        message(FATAL_ERROR "tools/build_quic.sh failed")
    endif()
endif()
file(LOCK "${RAWFRAME_QUIC_PREFIX}.lock" RELEASE)

find_package(Threads REQUIRED)
add_library(msquic::msquic STATIC IMPORTED GLOBAL)
set_target_properties(msquic::msquic PROPERTIES
    IMPORTED_LOCATION "${RAWFRAME_QUIC_PREFIX}/lib/libmsquic.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/msquic/src/inc;${RAWFRAME_QUIC_PREFIX}/include"
    INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")
# On macOS MsQuic checks certificates through the system's trust store
# (D236).
if(APPLE)
    set_property(TARGET msquic::msquic APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 "-framework Security" "-framework CoreFoundation")
endif()

# OpenSSL's libcrypto alone, from the same build, for Ed25519 signatures
# (SPEC-0019: engine-side verification through OpenSSL's EVP one-shot
# interface). libmsquic.a carries the same objects, and a binary may link
# both: the linker takes each symbol once.
add_library(openssl::crypto STATIC IMPORTED GLOBAL)
set_target_properties(openssl::crypto PROPERTIES
    IMPORTED_LOCATION "${RAWFRAME_QUIC_PREFIX}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWFRAME_QUIC_PREFIX}/include"
    INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")
