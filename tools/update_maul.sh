#!/usr/bin/env bash
# Replaces third_party/<library> with the library sources of one exact Maul
# release archive, checked against its SHA256SUMS first, or of one exact
# revision, taken from a checkout's object store, never from its working
# tree. Maul2D and Maul3D keep the build file we write beside their sources;
# the other libraries bring their own CMake, which the engine configures
# with its options (third_party/maul_*.cmake).
#
#   tools/update_maul.sh <library> <release archive>
#   tools/update_maul.sh <library> <checkout> <revision>
#
# with <library> one of maul2d, maul3d, maul-unicode, maul-window, maul-rhi,
# maul-ui, maul-tween, maul-nav, maul-audio.
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

engine="$1"
case "$engine" in
maul2d | maul3d) paths=(include src LICENSE) ;;
maul-unicode) paths=(include src cmake CMakeLists.txt LICENSE) ;;
maul-window) paths=(include src cmake protocols java tools/gen_web_glue.py CMakeLists.txt LICENSE) ;;
maul-rhi) paths=(include src khronos directx cmake tools/mrhi_container.py tools/mrhi_msl.py tools/mrhi_dxil.py tools/gen_web_glue.py contract/mrhi.json CMakeLists.txt LICENSE) ;;
maul-ui) paths=(include src cmake java tools/gen_web_glue.py CMakeLists.txt LICENSE docs/user/licenses.md) ;;
maul-tween) paths=(include src cmake CMakeLists.txt LICENSE) ;;
maul-nav) paths=(include src cmake CMakeLists.txt LICENSE) ;;
maul-audio) paths=(include src cmake CMakeLists.txt LICENSE) ;;
*)
    echo "update_maul.sh: the library is maul2d, maul3d, maul-unicode, maul-window, maul-rhi, maul-ui, maul-tween, maul-nav, or maul-audio" >&2
    exit 2
    ;;
esac
target="third_party/$engine"
if [[ $# -eq 2 ]]; then
    archive="$2"
    # The release's SHA256SUMS lies beside its archive and must name it.
    sums="$(dirname "$archive")/SHA256SUMS"
    line="$(grep -E "^[0-9a-f]{64}  $(basename "$archive")\$" "$sums")" || {
        echo "update_maul.sh: $sums does not name $(basename "$archive")" >&2
        exit 2
    }
    (cd "$(dirname "$archive")" && sha256sum -c --strict <<<"$line")
    # One top folder, as a release archive has: its name is the release.
    top="$(tar -tzf "$archive" | sed -n 1p)"
    top="${top%%/*}"
    revision="$top"
else
    checkout="$2"
    revision="$(git -C "$checkout" rev-parse --verify "$3^{commit}")"
fi

keep="$(mktemp)"
if [[ "$engine" == maul2d || "$engine" == maul3d ]] && [[ -f "$target/CMakeLists.txt" ]]; then
    cp "$target/CMakeLists.txt" "$keep"
fi
rm -rf "$target"
mkdir -p "$target"
if [[ $# -eq 2 ]]; then
    tar -x -m -z -f "$archive" -C "$target" --strip-components=1 "${paths[@]/#/$top/}"
else
    git -C "$checkout" archive "$revision" "${paths[@]}" | tar -x -m -C "$target"
fi
if [[ -s "$keep" ]]; then
    cp "$keep" "$target/CMakeLists.txt"
fi
rm "$keep"
echo "$target is now $revision; update the table in docs/operations/dependencies.md"
if [[ "$engine" == maul2d || "$engine" == maul3d ]]; then
    echo "and the source list in $target/CMakeLists.txt if upstream's changed"
fi
