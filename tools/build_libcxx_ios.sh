#!/usr/bin/env bash
# Builds LLVM 20.1.8's libc++ and libc++abi for the iOS simulator on 64-bit
# ARM, static, into one prefix (D583): the library the engine's C++ is
# compiled and linked against there (cmake/ios-simulator.cmake). Xcode's own
# libc++ has no std::from_chars of a double before iOS 26, and Homebrew's
# LLVM links the SDK's library, so LLVM's is built from its released source,
# checked by its SHA-256, on Apple's own unwinder. Runs on macOS with
# Xcode; takes under a minute.
#
#   tools/build_libcxx_ios.sh <prefix> [<llvm prefix>]
#
# The LLVM prefix is Homebrew's llvm@20 unless named. The source tarball is
# kept beside the prefix, so a second build fetches nothing.
set -euo pipefail

prefix="$1"
llvm="${2:-$(brew --prefix llvm@20)}"
version=20.1.8
sha256=6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d
target=arm64-apple-ios16.3-simulator
sdk="$(xcrun --sdk iphonesimulator --show-sdk-path)"
tarball="$(dirname "$prefix")/llvm-project-$version.src.tar.xz"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ ! -f "$tarball" ]; then
    mkdir -p "$(dirname "$tarball")"
    curl -sSfL -o "$tarball.part" \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/llvm-project-$version.src.tar.xz"
    mv "$tarball.part" "$tarball"
fi
echo "$sha256  $tarball" | shasum -a 256 -c - >/dev/null
tar -xf "$tarball" -C "$work"

rm -rf "$prefix"
cmake -S "$work/llvm-project-$version.src/runtimes" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$llvm/bin/clang" -DCMAKE_CXX_COMPILER="$llvm/bin/clang++" \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.3 -DCMAKE_C_COMPILER_TARGET="$target" -DCMAKE_CXX_COMPILER_TARGET="$target" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$prefix" >"$work/log" 2>&1 &&
    cmake --build "$work/build" >>"$work/log" 2>&1 &&
    cmake --install "$work/build" >>"$work/log" 2>&1 || { tail -40 "$work/log"; exit 1; }
touch "$prefix/complete"
