#!/usr/bin/env bash
# A web export whose web player is not there stops before anything is
# cooked, saying what is missing and where, and leaves no folder behind:
# it once failed a minute in, its folder half made.
#
#   missing.sh <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <game directory> <work directory>
set -uo pipefail

work="$6"
rm -rf "$work"
mkdir -p "$work"
"$1" "$5" "$work/out" --target web --cook "$2" --build "$3" --server "$4" \
    --web-client "$work/nowhere/rawframe-web-client.wasm" >"$work/said.txt" 2>&1
echo "export: $?"
cat "$work/said.txt"
echo "folder left: $([ -e "$work/out" ] && echo yes || echo no)"
