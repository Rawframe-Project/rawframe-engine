#!/usr/bin/env bash
# A game exported (D396), signed by a studio's own publisher key, and played
# from its folder as a player would: the folder's launcher starts its
# server, then its client in a real window (here for five seconds, then it
# ends on its own). Prints the keys the folder holds (the studio's key set,
# never its secret), the fonts the client read from the folder's library,
# the players admitted, the frames shown, each side's exit, and the
# launcher's code. A
# machine with no adapter the client allows skips, unless
# RAWFRAME_REQUIRE_GPU is set. Run under an X server, from the repository
# root.
#
#   exported.sh <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <rawframe-client>
#               <rawframe-play> <game directory> <output directory>
set -uo pipefail

out="$8"
rm -rf "$out" "$out.keys"
# The studio's key, made apart from the folder.
kid="$("$3" key studio "$out.keys" | cut -d' ' -f2)"
# A port nothing holds right now.
port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
"$1" "$7" "$out" --port "$port" --key "$out.keys/$kid.key" --publisher studio --cook "$2" --build "$3" \
    --server "$4" --client "$5" --play "$6" || exit 1
printf 'host.maximum_iterations = 600\nrender.device = any\n' >>"$out/client.conf"
status=0
"$out/rawframe-play" || status=$?
if grep -q '"device_unavailable"' "$out/client.log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
field() {
    grep -o "\"$2\":[^,}]*" "$out/$1" | head -1 | cut -d: -f2 | tr -d '"'
}
echo "export: keys $(ls "$out/library/keys" | tr '\n' ' ')fonts read $(field client.log fontsRead), admitted $(field client.log admitted)," \
    "frames shown $(field client.log framesShown), client $(field client.log exit), server $(field server.log exit)," \
    "play exit $status"
