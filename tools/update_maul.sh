#!/usr/bin/env bash
# Replaces third_party/<library> with the library sources of one exact Maul
# revision, taken from a checkout's object store, never from its working
# tree. Maul2D and Maul3D keep the build file we write beside their sources;
# Maul Unicode and Maul Window bring their own CMake, which the engine
# configures with its options (third_party/maul_window.cmake).
#
#   tools/update_maul.sh <maul2d|maul3d|maul-unicode|maul-window|maul-rhi|maul-ui|maul-nav|maul-audio> <checkout> <revision>
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

engine="$1"
case "$engine" in
maul2d | maul3d) paths=(include src LICENSE) ;;
maul-unicode) paths=(include src cmake CMakeLists.txt LICENSE) ;;
maul-window) paths=(include src cmake protocols tools/gen_web_glue.py CMakeLists.txt LICENSE) ;;
maul-rhi) paths=(include src khronos cmake tools/mrhi_container.py tools/mrhi_msl.py tools/gen_web_glue.py docs/contract/mrhi.json CMakeLists.txt LICENSE) ;;
maul-ui) paths=(include src cmake CMakeLists.txt LICENSE THIRD_PARTY.md) ;;
maul-nav) paths=(include src cmake CMakeLists.txt LICENSE) ;;
maul-audio) paths=(include src cmake CMakeLists.txt LICENSE) ;;
*)
    echo "update_maul.sh: the library is maul2d, maul3d, maul-unicode, maul-window, maul-rhi, maul-ui, maul-nav, or maul-audio" >&2
    exit 2
    ;;
esac
checkout="$2"
revision="$(git -C "$checkout" rev-parse --verify "$3^{commit}")"
target="third_party/$engine"

keep="$(mktemp)"
if [[ "$engine" == maul2d || "$engine" == maul3d ]] && [[ -f "$target/CMakeLists.txt" ]]; then
    cp "$target/CMakeLists.txt" "$keep"
fi
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" "${paths[@]}" | tar -x -m -C "$target"
if [[ -s "$keep" ]]; then
    cp "$keep" "$target/CMakeLists.txt"
fi
rm "$keep"
echo "$target is now $revision; update the table in third_party/README.md"
if [[ "$engine" == maul2d || "$engine" == maul3d ]]; then
    echo "and the source list in $target/CMakeLists.txt if upstream's changed"
fi
