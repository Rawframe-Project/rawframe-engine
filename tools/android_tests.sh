#!/usr/bin/env bash
# Runs an Android tree's test programs on a device or emulator (D561):
#
#   tools/android_tests.sh <build tree> [<pattern>]
#
# ADB is the adb command with its device (`adb` when unset), for example
# ADB="adb -P 5139 -s emulator-5590". The source tree's files the tests read
# go to /data/local/tmp/rawframe/files and the programs to
# /data/local/tmp/rawframe/programs, the places the android-development
# preset builds into the tests (RAWFRAME_TEST_FILES, RAWFRAME_TEST_PROGRAMS).
# Each program whose name holds the pattern runs alone, in a scratch
# directory of its own, for at most five minutes; the run fails when one
# does.
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$1"
pattern="${2:-}"
adb=(${ADB:-adb})
device=/data/local/tmp/rawframe
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

find "$tree" -type f -perm -u+x \( -name 'rawframe_*_tests' -o -name rawframe_process_child \) | sort >"$work/programs"
tar -cf "$work/files.tar" games tests modules/*/tests modules/kest_library/kest third_party/kest/lib
"${adb[@]}" shell "rm -rf $device && mkdir -p $device/files $device/programs $device/scratch"
"${adb[@]}" push -q "$work/files.tar" "$device/files.tar"
"${adb[@]}" shell "cd $device/files && tar -xf ../files.tar && rm ../files.tar"
# shellcheck disable=SC2046
"${adb[@]}" push -q $(cat "$work/programs") "$device/programs/"
"${adb[@]}" shell "chmod 755 $device/programs/*"

failed=0
for program in $(xargs -n1 basename <"$work/programs" | grep '_tests$' | grep -F -- "$pattern"); do
    scratch="$device/scratch/$program"
    said="$("${adb[@]}" shell "mkdir -p $scratch && cd $scratch && TMPDIR=$scratch timeout 300 \
        $device/programs/$program >output 2>&1; echo \"exit \$?\"; tail -1 output" | tr -d '\r')"
    if [ "$(head -1 <<<"$said")" = "exit 0" ]; then
        echo "ok   $program: $(tail -1 <<<"$said")"
    else
        failed=$((failed + 1))
        echo "FAIL $program: $(tr '\n' ' ' <<<"$said")"
        "${adb[@]}" shell "grep -v '^ok' $scratch/output | head -20" | sed 's/^/    /'
    fi
done
echo "android tests: $failed failed"
[ "$failed" -eq 0 ]
