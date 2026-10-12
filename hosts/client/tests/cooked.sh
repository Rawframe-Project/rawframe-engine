#!/usr/bin/env bash
# A game's sources cooked by the cook tool, which prints its report. A
# machine without the shader toolchain (D484), where the cook refuses a
# material that needs its own program, skips, unless
# RAWFRAME_REQUIRE_SHADER_TOOLCHAIN is set, as the check sets it where the
# toolchain is installed (D701).
#
# Given --then, a test that plays the cooked content runs only where it was
# cooked, its content manifest written; where it was not, it skips as the
# cook did (D707).
#
# usage: cooked.sh <rawframe-cook> <sources> <output> <cache>
#        cooked.sh --then <output> <command> [arguments]
set -uo pipefail

if [ "$1" = "--then" ]; then
    if [ ! -f "$2/content.manifest" ] && [ -z "${RAWFRAME_REQUIRE_SHADER_TOOLCHAIN:-}" ]; then
        echo "skip: no shader toolchain"
        exit 0
    fi
    shift 2
    exec "$@"
fi
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
