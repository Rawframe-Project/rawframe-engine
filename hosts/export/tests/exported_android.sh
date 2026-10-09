#!/usr/bin/env bash
# A game exported for Android (D555), shown on this machine: the package's
# client configuration pins the identity the exported server holds, and the
# exported server, started as it was written, admits a bot that plays from
# the package's own library with the package's pin, reaching the server on
# this machine's loopback in place of the address a phone would use. Prints
# one summary line.
#
#   exported_android.sh <rawframe-export> <rawframe-cook> <rawframe-build>
#                       <rawframe-server> <rawframe-bots> <game directory> <output>
set -uo pipefail

export_tool="$1"
cook="$2"
build="$3"
server="$4"
bots="$5"
game="$6"
out="$7"
rm -rf "$out"
# A port nothing holds: the check's trees run this test at once, and a
# game's own port is one (D402), so a bot could reach another tree's server.
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
"$export_tool" "$game" "$out" --target android --address 10.0.2.2 --port "$port" --cook "$cook" \
    --build "$build" --server "$server" || exit 1
pin=$(sed -n 's/^network.quic.pin = //p' "$out/android/game/client.conf")
held=$(openssl x509 -in "$out/server/identity.pem" -outform der | sha256sum | cut -d' ' -f1)
pinned=no
[ -n "$pin" ] && [ "$pin" = "$held" ] && pinned=yes
target=$(grep -o '"target":"[a-z]*"' "$out/export.receipt")

(cd "$out/server" && exec ./rawframe-server --config server.conf) >"$out/server.log" 2>&1 &
served=$!
for _ in $(seq 200); do
    grep -q '"code":"listening"' "$out/server.log" 2>/dev/null && break
    sleep 0.05
done
# The package's configuration, but for where the server is and how long the
# bot plays.
sed -e "s/^bots.endpoint = .*/bots.endpoint = 127.0.0.1:$port/" -e '/^bots.player/d' -e '/^render\./d' \
    -e '/^audio\./d' -e '/^scene\./d' "$out/android/game/client.conf" >"$out/android/game/bots.conf"
printf 'host.maximum_iterations = 360\nbots.count = 1\n' >>"$out/android/game/bots.conf"
"$bots" --config "$out/android/game/bots.conf" >"$out/bots.log" 2>&1
kill -INT "$served" 2>/dev/null
wait "$served"
admitted=$(grep -o '"admitted":[0-9]*' "$out/bots.log" | head -1 | cut -d: -f2)
echo "android export: $target, pinned to the server's identity: $pinned, admitted ${admitted:-0}"
