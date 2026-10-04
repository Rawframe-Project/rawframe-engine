#!/usr/bin/env bash
# A game exported (D396), signed by a studio's own publisher key, and played
# from its folder as a player would: the folder's launcher starts its
# server, then its client in a real window (here for five seconds, then it
# ends on its own). Prints the keys the folder holds (the studio's key set,
# never its secret), the fonts the client read from the folder's library,
# the players admitted, the frames shown, how each side stopped, and whether
# the launcher ended with the client's code. A
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
# Each side's own record of how it stopped. A window drawn on a software
# rasterizer under the check's load may stop in controlled overload; the
# launcher ends with the client's code, whatever it is.
stopped() {
    grep '"code":"stopped"' "$out/$1" | grep -o "\"$2\":[^,}]*" | head -1 | cut -d: -f2 | tr -d '"'
}
same=no
[ "$(stopped client.log exitCode)" = "$status" ] && same=yes
echo "export: keys $(ls "$out/library/keys" | tr '\n' ' ')fonts read $(field client.log fontsRead)," \
    "admitted $(field client.log admitted), frames shown $(field client.log framesShown)," \
    "client stopped $(stopped client.log exit), server stopped $(stopped server.log exit)," \
    "the launcher's code the client's: $same"
