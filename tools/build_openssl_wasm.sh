#!/usr/bin/env bash
# Builds the vendored OpenSSL's libcrypto for the web build (wasm32-wasi
# without threads) into one prefix, for Ed25519 verification (SPEC-0019).
# Configure runs this once per machine and pinned revision
# (third_party/openssl_wasm.cmake), as tools/build_quic.sh does natively.
# No threads, sockets, assembly, or modules: libcrypto alone. WASI's
# emulations stand in for the signals, clocks, process identity, and
# mappings OpenSSL still names, and cmake/openssl_wasi_shim.h for the
# chmod wasi-libc lacks; OpenSSL's own sources are unchanged.
#
#   tools/build_openssl_wasm.sh <prefix> <c compiler>
set -euo pipefail
cd "$(dirname "$0")/.."

prefix="$1"
compiler="$2"
root="$PWD"
jobs="$(nproc)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

rm -rf "$prefix"
mkdir -p "$work/openssl"
(
    cd "$work/openssl"
    CC="$compiler --target=wasm32-wasi" AR=llvm-ar-20 RANLIB=llvm-ranlib-20 perl "$root/third_party/openssl/Configure" \
        linux-generic32 no-asm no-threads no-shared no-sock no-dso no-ui-console no-afalgeng no-apps no-tests \
        no-engine no-async no-secure-memory no-dgram no-http no-ocsp no-cmp no-ct no-srp no-docs no-legacy \
        -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_MMAN \
        -include"$root/cmake/openssl_wasi_shim.h" --prefix="$prefix" --libdir=lib >"$work/openssl.log" 2>&1
    make -j"$jobs" build_generated >>"$work/openssl.log" 2>&1
    make -j"$jobs" libcrypto.a >>"$work/openssl.log" 2>&1
    mkdir -p "$prefix/lib" "$prefix/include"
    cp libcrypto.a "$prefix/lib/"
    cp -r "$root/third_party/openssl/include/openssl" "$prefix/include/"
    cp include/openssl/*.h "$prefix/include/openssl/"
) || { tail -30 "$work/openssl.log"; exit 1; }
touch "$prefix/complete"
