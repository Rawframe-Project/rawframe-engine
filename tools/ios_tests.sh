#!/usr/bin/env bash
# Runs an iOS simulator tree's test programs in a booted simulator (D584):
#
#   tools/ios_tests.sh <build tree> [<pattern>]
#
# RAWFRAME_IOS_DEVICE names the simulator, the booted one when unset. A
# simulator's processes read this machine's files where they are, so the
# tests read the source tree as the tree was built to (RAWFRAME_TEST_FILES)
# and start programs in the build tree. Each program whose name holds the
# pattern runs alone, its temporary files in a scratch directory of its own,
# for at most five minutes; the run fails when one does.
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$1"
pattern="${2:-}"
device="${RAWFRAME_IOS_DEVICE:-booted}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failed=0
passed=0
for program in $(find "$tree" -type f -perm -u+x -path '*.app/rawframe_*_tests' | sort); do
    name="$(basename "$program")"
    grep -qF -- "$pattern" <<<"$name" || continue
    scratch="$work/$name"
    mkdir -p "$scratch"
    # simctl hands a SIMCTL_CHILD_ variable to the process without its
    # prefix; macOS has no timeout(1), so Perl's alarm bounds the run.
    status=0
    SIMCTL_CHILD_TMPDIR="$scratch" perl -e 'alarm shift; exec @ARGV' 300 \
        xcrun simctl spawn "$device" "$program" >"$scratch/output" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        passed=$((passed + 1))
        echo "ok   $name: $(tail -1 "$scratch/output")"
    else
        failed=$((failed + 1))
        echo "FAIL $name: exit $status, $(tail -1 "$scratch/output")"
        grep -v '^ok' "$scratch/output" | head -20 | sed 's/^/    /'
    fi
done
echo "ios tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
