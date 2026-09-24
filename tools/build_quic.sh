#!/usr/bin/env bash
# Builds the vendored OpenSSL and MsQuic into one prefix, as static libraries.
# Configure runs this once per machine and pinned revision (third_party/
# quic.cmake): the result is the same for every build tree, and building
# OpenSSL for each of six presets would cost more than the engine does.
#
#   tools/build_quic.sh <prefix> <c compiler> <c++ compiler>
set -euo pipefail
cd "$(dirname "$0")/.."

prefix="$1"
compiler="$2"
cxx_compiler="$3"
root="$PWD/third_party"
jobs="$(nproc)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

rm -rf "$prefix"
mkdir -p "$work/openssl" "$work/msquic"

# OpenSSL builds out of tree. No shared objects, tests, docs, or programs:
# MsQuic links the two libraries and nothing else.
(
    cd "$work/openssl"
    CC="$compiler" perl "$root/openssl/Configure" linux-x86_64 no-shared no-tests no-docs no-apps \
        --prefix="$prefix" --libdir=lib -fPIC >"$work/openssl.log" 2>&1
    make -j"$jobs" build_libs >>"$work/openssl.log" 2>&1
    make install_dev >>"$work/openssl.log" 2>&1
) || { tail -30 "$work/openssl.log"; exit 1; }

# MsQuic with OpenSSL as its TLS library, logging compiled out. Its warning
# flags are chosen by the C++ compiler, so that is named too.
(
    cmake -S "$root/msquic" -B "$work/msquic" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DQUIC_TLS_LIB=openssl -DQUIC_OPENSSL_INCLUDE_DIR="$prefix/include" \
        -DQUIC_OPENSSL_LIB_DIR="$prefix/lib" -DQUIC_BUILD_SHARED=OFF -DQUIC_BUILD_TEST=OFF \
        -DQUIC_BUILD_TOOLS=OFF -DQUIC_BUILD_PERF=OFF -DQUIC_ENABLE_LOGGING=OFF >"$work/msquic.log" 2>&1
    cmake --build "$work/msquic" -j"$jobs" >>"$work/msquic.log" 2>&1
) || { tail -30 "$work/msquic.log"; exit 1; }

# The MsQuic headers are read from third_party/msquic/src/inc.
cp "$work/msquic/bin/Release/libmsquic.a" "$prefix/lib/"
touch "$prefix/complete"
