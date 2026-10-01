#!/usr/bin/env bash
# Vendors the text stack Maul UI's text component builds on (D383): FreeType
# and HarfBuzz at the releases its cmake/TextDependencies.cmake pins, checked
# by the same SHA-256, with only what that file compiles. Maul UI's build
# then finds them through FETCHCONTENT_SOURCE_DIR_* (third_party/maul_ui.cmake)
# and nothing is fetched. Maul Unicode is vendored on its own.
#
# usage: tools/update_text_deps.sh <freetype-2.14.3.tar.xz> <harfbuzz-14.5.1.tar.xz>
#
# Moving Maul UI's pin may move these: read its TextDependencies.cmake, change
# the versions and hashes below, and run this again.
set -euo pipefail
cd "$(dirname "$0")/.."

freetype_version=2.14.3
freetype_sha256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
harfbuzz_version=14.5.1
harfbuzz_sha256=7e2fa4e8c7c98e8d8140671f5772542afaaa6acccfbd746506886b6d85f7f8d6

check() {
    local archive="$1" expected="$2"
    local actual
    actual="$(sha256sum "$archive" | cut -d' ' -f1)"
    if [[ "$actual" != "$expected" ]]; then
        echo "update_text_deps.sh: $archive is $actual, not the pinned $expected" >&2
        exit 1
    fi
}

check "$1" "$freetype_sha256"
check "$2" "$harfbuzz_sha256"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tar -xJf "$1" -C "$work"
tar -xJf "$2" -C "$work"

freetype="$work/freetype-$freetype_version"
rm -rf third_party/freetype
mkdir -p third_party/freetype/src third_party/freetype/docs
cp -R "$freetype/include" third_party/freetype/
for module in base cff psaux pshinter psnames sfnt smooth truetype; do
    cp -R "$freetype/src/$module" third_party/freetype/src/
done
cp "$freetype/LICENSE.TXT" third_party/freetype/
cp "$freetype/docs/FTL.TXT" third_party/freetype/docs/

harfbuzz="$work/harfbuzz-$harfbuzz_version"
rm -rf third_party/harfbuzz
mkdir -p third_party/harfbuzz
cp -R "$harfbuzz/src" third_party/harfbuzz/
cp "$harfbuzz/COPYING" third_party/harfbuzz/
# Its build files, scripts, and generators are not what Maul UI compiles.
find third_party/harfbuzz/src \( -name '*.py' -o -name 'meson.build' -o -name '*.sh' -o -name 'Makefile*' \
    -o -name '*.rl' -o -name '*.txt' \) -delete
echo "third_party/freetype is $freetype_version and third_party/harfbuzz is $harfbuzz_version"
