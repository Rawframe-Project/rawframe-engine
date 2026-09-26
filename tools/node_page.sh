#!/usr/bin/env bash
# Runs a test page under Node and ends with the page's verdict, the line
# `page: passed` or `page: failed` it writes last (verdict.mjs). Node 24 has
# been seen to hang in its own teardown after a page had passed (D238): a
# Node still running five seconds after its page's verdict is killed, and
# says so, and the verdict stands. A page that writes none in two minutes
# fails.
#
#   tools/node_page.sh <page.mjs> [arguments...]
set -uo pipefail

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
node --no-warnings "$@" >"$log" 2>&1 &
node=$!

verdict_seen=0
for _ in $(seq 1200); do
    kill -0 "$node" 2>/dev/null || break
    if grep -qE '^page: (passed|failed)$' "$log"; then
        verdict_seen=1
        break
    fi
    sleep 0.1
done
if [ "$verdict_seen" = 1 ]; then
    for _ in $(seq 50); do
        kill -0 "$node" 2>/dev/null || break
        sleep 0.1
    done
fi
if kill -0 "$node" 2>/dev/null; then
    kill -KILL "$node"
    if [ "$verdict_seen" = 1 ]; then
        echo "node_page: Node did not exit after the page's verdict, and was killed (D238)" >>"$log"
    else
        echo "node_page: the page wrote no verdict in two minutes" >>"$log"
    fi
fi
wait "$node" 2>/dev/null
status=$?
cat "$log"
if grep -qx 'page: passed' "$log"; then
    exit 0
fi
exit $((status == 0 ? 1 : status))
