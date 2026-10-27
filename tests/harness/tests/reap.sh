#!/usr/bin/env bash
# The reaper the check runs ctest under (tools/reap.py) ends what a command
# leaves running: an orphan and the child the orphan started, which is
# handed to the reaper only once the orphan is killed, both named and
# neither left alive, the run failing. A command that leaves nothing keeps
# its own status. Linux only.
#
# usage: reap.sh <tools/reap.py>
set -uo pipefail

reap=$1
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A shell that starts an orphan, itself waiting on a sleeping child.
python3 "$reap" --grace 0 bash -c '
    sh -c "sleep 300 & echo \$! >\"$1/grandchild\"; wait" &
    echo $! >"$1/orphan"
    while [ ! -s "$1/grandchild" ]; do sleep 0.05; done' _ "$work" >"$work/out" 2>&1
status=$?
cat "$work/out"
[ "$status" -eq 1 ] || { echo "the run did not fail: $status"; exit 1; }
for name in orphan grandchild; do
    pid=$(cat "$work/$name")
    grep -q "killed: $pid " "$work/out" || { echo "the $name ($pid) was not named"; exit 1; }
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid"
        echo "the $name ($pid) was left alive"
        exit 1
    fi
done
grep -q '^reap: 2 process(es) outlived' "$work/out" || { echo "the count is not two"; exit 1; }

python3 "$reap" --grace 0 true || { echo "a clean command failed"; exit 1; }
python3 "$reap" --grace 0 bash -c 'exit 3'
[ $? -eq 3 ] || { echo "a command's own status was not kept"; exit 1; }
echo "the reaper ended an orphan and its child"
