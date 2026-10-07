#!/usr/bin/env bash
# A game exported (D396), signed by a studio's own publisher key, and played
# from its folder as a player would: the folder's launcher starts its
# server, then its client in a real window, and is asked to stop three
# seconds after the player is admitted, as a player closing the game
# does, which it passes on to the client and then the server. Under CI's
# load the address sanitizer's tree has taken more than five seconds to
# admit the player; the client ends on its own after a minute regardless.
# Prints the keys the folder holds (the studio's key set, never its
# secret), the fonts the client read from the folder's library, the
# players admitted, the frames shown, how each side stopped, and whether
# the launcher ended with the client's code. A machine with no adapter the
# client allows skips, unless RAWFRAME_REQUIRE_GPU is set. Run under an X
# server, from the repository root.
#
#   exported.sh <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <rawframe-client>
#               <rawframe-play> <game directory> <output directory>
set -uo pipefail

out="$8"
rm -rf "$out" "$out.keys"
# The studio's key, made apart from the folder.
kid="$("$3" key studio "$out.keys" | cut -d' ' -f2)"
# A port nothing holds right now.
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
"$1" "$7" "$out" --port "$port" --key "$out.keys/$kid.key" --publisher studio --cook "$2" --build "$3" \
    --server "$4" --client "$5" --play "$6" || exit 1
printf 'host.maximum_iterations = 7200\nrender.device = any\n' >>"$out/client.conf"
"$out/rawframe-play" &
play=$!
for _ in $(seq 600); do
    grep -q '"code":"bots_admitted"' "$out/client.log" 2>/dev/null && break
    kill -0 "$play" 2>/dev/null || break
    sleep 0.1
done
sleep 3
kill -TERM "$play" 2>/dev/null
status=0
wait "$play" || status=$?
if grep -q '"device_unavailable"' "$out/client.log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
field() {
    grep -o "\"$2\":[^,}]*" "$out/$1" | head -1 | cut -d: -f2 | tr -d '"'
}
# Each side's own record of how it stopped. A window drawn on a software
# rasterizer under the check's load may stop in controlled overload; the
# launcher ends with the client's code, whatever it is.
stopped() {
    grep '"code":"stopped"' "$out/$1" | grep -o "\"$2\":[^,}]*" | head -1 | cut -d: -f2 | tr -d '"'
}
same=no
[ "$(stopped client.log exitCode)" = "$status" ] && same=yes
# Where the client never started, the server's own record says why: how
# it stopped and its last lines, a sanitizer's report among them.
if [ ! -f "$out/client.log" ]; then
    echo "the client never started; the launcher ended $status; the server's log ends:"
    tail -n 20 "$out/server.log" 2>/dev/null | cut -c1-400
fi
echo "export: keys $(ls "$out/library/keys" | tr '\n' ' ')fonts read $(field client.log fontsRead)," \
    "admitted $(field client.log admitted), frames shown $(field client.log framesShown)," \
    "client stopped $(stopped client.log exit), server stopped $(stopped server.log exit)," \
    "the launcher's code the client's: $same"
