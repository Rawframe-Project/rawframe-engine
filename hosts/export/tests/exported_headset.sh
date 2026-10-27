#!/usr/bin/env bash
# Reach exported to be shown on a headset: the client's
# configuration asks for one, the folder's README says so, and the exported
# server, started as it was written, admits a bot that plays from the
# folder's library with the client's pin. An export for the web is refused
# a headset. Prints one summary line.
#
#   exported_headset.sh <rawframe-export> <rawframe-cook> <rawframe-build>
#                       <rawframe-server> <rawframe-client> <rawframe-play>
#                       <rawframe-bots> <game directory> <output>
set -uo pipefail

export_tool="$1"
bots="$7"
game="$8"
out="$9"
rm -rf "$out" "$out.web"
# A port nothing holds: the check's trees run this test at once.
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
"$export_tool" "$game" "$out" --port "$port" --cook "$2" --build "$3" --server "$4" --client "$5" --play "$6" \
    --headset true || exit 1
asked=no
grep -qx 'xr.headset = true' "$out/client.conf" && asked=yes
told=no
grep -q 'VR headset' "$out/README.txt" && told=yes
web=accepted
"$export_tool" "$game" "$out.web" --target web --headset true >/dev/null 2>&1 || web=refused

printf 'host.maximum_iterations = 3600\n' >>"$out/server.conf"
(cd "$out" && exec ./rawframe-server --config server.conf) >"$out/server.log" 2>&1 &
served=$!
for _ in $(seq 200); do
    grep -q '"code":"listening"' "$out/server.log" 2>/dev/null && break
    sleep 0.05
done
# The client's configuration, but for how long the bot plays and what a
# bots process draws: nothing.
sed -e '/^bots.player/d' -e '/^render\./d' -e '/^audio\./d' -e '/^scene\./d' -e '/^xr\./d' -e '/^ui\./d' \
    -e '/^window\./d' "$out/client.conf" >"$out/bots.conf"
printf 'host.maximum_iterations = 360\nbots.count = 1\n' >>"$out/bots.conf"
(cd "$out" && exec "$bots" --config bots.conf) >"$out/bots.log" 2>&1
kill -INT "$served" 2>/dev/null
wait "$served"
admitted=$(grep -o '"admitted":[0-9]*' "$out/bots.log" | head -1 | cut -d: -f2)
echo "headset export: asked $asked, told $told, web $web, admitted ${admitted:-0}"
