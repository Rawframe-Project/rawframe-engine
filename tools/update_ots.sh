#!/usr/bin/env bash
# Vendors the OpenType Sanitizer (D385), the cook's font sanitizer under
# ADR-0078: its release archive, checked by SHA-256, with its library's
# sources and headers and its license. Graphite's tables are left out (their
# sanitizers need lz4, and a font's Graphite tables are dropped like any table
# OTS does not sanitize), and so are its tools, tests, and build files.
#
# usage: tools/update_ots.sh <ots-9.3.0.tar.xz>
#
# Moving the pin: change the version and hash below, run this again, read the
# release's notes for new tables or dependencies, and update
# third_party/ots.cmake's sources and third_party/README.md.
set -euo pipefail
cd "$(dirname "$0")/.."

version=9.3.0
sha256=23814f8e90ee77379f54e86a012c09bba2d133940e5257546b29cf087a73beec

actual="$(sha256sum "$1" | cut -d' ' -f1)"
if [[ "$actual" != "$sha256" ]]; then
    echo "update_ots.sh: $1 is $actual, not the pinned $sha256" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tar -xJf "$1" -C "$work"
ots="$work/ots-$version"

# CMakeLists.txt and rawframe/ are ours and stay.
rm -rf third_party/ots/include third_party/ots/src third_party/ots/LICENSE third_party/ots/README.md
mkdir -p third_party/ots/src
cp -R "$ots/include" third_party/ots/
cp "$ots"/src/*.cc "$ots"/src/*.h third_party/ots/src/
for graphite in feat glat gloc sile silf sill; do
    rm third_party/ots/src/$graphite.cc third_party/ots/src/$graphite.h
done
rm third_party/ots/src/graphite.h
cp "$ots/LICENSE" "$ots/README.md" third_party/ots/
echo "third_party/ots is $version"
