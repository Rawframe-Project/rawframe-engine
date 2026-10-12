#!/usr/bin/env bash
# A game's sources cooked by the cook tool, which prints its report. A
# machine without the shader toolchain (D484), where the cook refuses a
# material that needs its own program, skips, unless
# RAWFRAME_REQUIRE_SHADER_TOOLCHAIN is set, as the check sets it where the
# toolchain is installed (D701).
#
# usage: cooked.sh <rawframe-cook> <sources> <output> <cache>
set -uo pipefail

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
"$1" "$2" "$3" "$4" >"$log" 2>&1
status=$?
cat "$log"
if grep -q "the shader toolchain builds, and the cook was given none" "$log" &&
    [ -z "${RAWFRAME_REQUIRE_SHADER_TOOLCHAIN:-}" ]; then
    echo "skip: no shader toolchain"
fi
exit "$status"
