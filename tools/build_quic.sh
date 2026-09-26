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
jobs="$(getconf _NPROCESSORS_ONLN)"
# OpenSSL's name for this machine (D236).
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) openssl_target=linux-x86_64 ;;
    Linux-aarch64) openssl_target=linux-aarch64 ;;
    Darwin-arm64) openssl_target=darwin64-arm64-cc ;;
    Darwin-x86_64) openssl_target=darwin64-x86_64-cc ;;
    MINGW64_NT*-x86_64 | MSYS_NT*-x86_64) openssl_target=VC-WIN64A ;;
    *) echo "build_quic: no OpenSSL target for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac
# Windows builds both with MSVC from Git's bash, inside a developer
# environment (D237): OpenSSL through its own perl and nmake, since Git's perl
# is not the Windows perl OpenSSL's Configure asks for, and both with the
# dynamic C runtime the engine uses.
windows=0
case "$openssl_target" in VC-*) windows=1 ;; esac
# Git's /usr/bin holds a GNU link that would shadow MSVC's linker.
if [ "$windows" = 1 ]; then
    PATH="$(dirname "$(cygpath -u "$compiler")"):$PATH"
    export PATH
fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

rm -rf "$prefix"
mkdir -p "$work/openssl" "$work/msquic"

# OpenSSL builds out of tree. No shared objects, tests, docs, or programs:
# MsQuic links the two libraries and nothing else.
if [ "$windows" = 1 ]; then
    (
        cd "$work/openssl"
        CC=cl "${RAWFRAME_PERL:-perl}" "$(cygpath -m "$root/openssl/Configure")" "$openssl_target" \
            no-shared no-tests no-docs no-apps no-asm \
            --prefix="$(cygpath -m "$prefix")" --openssldir="$(cygpath -m "$prefix/ssl")" --libdir=lib \
            >"$work/openssl.log" 2>&1
        nmake -nologo build_libs >>"$work/openssl.log" 2>&1
        nmake -nologo install_dev >>"$work/openssl.log" 2>&1
    ) || { tail -30 "$work/openssl.log"; exit 1; }
else
    (
        cd "$work/openssl"
        CC="$compiler" perl "$root/openssl/Configure" "$openssl_target" no-shared no-tests no-docs no-apps \
            --prefix="$prefix" --libdir=lib -fPIC >"$work/openssl.log" 2>&1
        make -j"$jobs" build_libs >>"$work/openssl.log" 2>&1
        make install_dev >>"$work/openssl.log" 2>&1
    ) || { tail -30 "$work/openssl.log"; exit 1; }
fi

# MsQuic with OpenSSL as its TLS library, logging compiled out. Its warning
# flags are chosen by the C++ compiler, so that is named too.
(
    cmake -S "$root/msquic" -B "$work/msquic" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DQUIC_TLS_LIB=openssl -DQUIC_OPENSSL_INCLUDE_DIR="$prefix/include" \
        -DQUIC_OPENSSL_LIB_DIR="$prefix/lib" -DQUIC_BUILD_SHARED=OFF -DQUIC_BUILD_TEST=OFF \
        -DQUIC_BUILD_TOOLS=OFF -DQUIC_BUILD_PERF=OFF -DQUIC_ENABLE_LOGGING=OFF \
        -DQUIC_STATIC_LINK_CRT=OFF -DQUIC_STATIC_LINK_PARTIAL_CRT=OFF >"$work/msquic.log" 2>&1
    cmake --build "$work/msquic" -j"$jobs" >>"$work/msquic.log" 2>&1
) || { tail -30 "$work/msquic.log"; exit 1; }

# The MsQuic headers are read from third_party/msquic/src/inc.
if [ "$windows" = 1 ]; then
    cp "$work/msquic/bin/Release/msquic.lib" "$prefix/lib/"
else
    cp "$work/msquic/bin/Release/libmsquic.a" "$prefix/lib/"
fi
touch "$prefix/complete"
