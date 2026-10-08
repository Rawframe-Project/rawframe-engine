#!/usr/bin/env bash
# A game exported to follow its channel (D434), as a studio ships one that
# updates: runners exported under the rawframe publisher's key with
# --follow naming the studio's mirror. The studio then publishes 0.2.0 on
# the mirror and points stable at it. The folder's launcher follows stable
# before it plays: its library installs 0.2.0, verified against the key set
# it pins, and the server and the client play 0.2.0's Composition, from
# the library alone. Played again with nothing new, the follow finds stable
# current; with the mirror gone, it fails and the launcher says so; and
# both times the game still plays 0.2.0. Prints, per play, whether both sides opened
# 0.2.0's Composition, how many follows installed one, and what the
# launcher said. A machine with no adapter the client
# allows skips, unless RAWFRAME_REQUIRE_GPU is set. Run under an X server,
# from the repository root.
#
#   followed.sh <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <rawframe-client>
#               <rawframe-play> <rawframe-install> <runners' cooked content> <work directory>
set -uo pipefail

build="$3"
cooked="$8"
work="$9"
rm -rf "$work"
mkdir -p "$work/mirror/keys"
kid="$("$build" key rawframe "$work/keys" | cut -d' ' -f2)"
cp "$work/keys/rawframe.keys" "$work/mirror/keys/"
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
out="$work/runners"
"$1" games/runners "$out" --port "$port" --key "$work/keys/$kid.key" --publisher rawframe --follow "$work/mirror" \
    --cook "$2" --build "$3" --server "$4" --client "$5" --play "$6" --install "$7" >"$work/export.txt" || exit 1
printf 'host.maximum_iterations = 240\n' >>"$out/client.conf"
printf 'host.maximum_iterations = 600\n' >>"$out/server.conf"

# The studio's 0.2.0, on the mirror and on stable.
"$build" "$cooked" "$work/build" rawframe/runners 0.2.0 linux x86_64 client build.development tool \
    "$work/keys/$kid.key" >/dev/null || exit 1
root="$("$build" install "$work/build" "$work/mirror" | cut -d' ' -f2)"
composition="$("$build" compose "$work/mirror" "$root" tool "$work/0.2.0.composition" | cut -d' ' -f2)"
"$build" release "$work/mirror" "$work/0.2.0.composition" 0.2.0 stable "$work/keys/$kid.key" >/dev/null || exit 1
echo "followed: published $composition"

play() {
    "$out/rawframe-play" 2>"$work/play-$1.txt"
    if grep -q '"device_unavailable"' "$out/client.log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
        echo "skip: no device"
        exit 0
    fi
    opened() {
        grep -o '"code":"composition_opened"[^}]*}[^}]*}' "$out/$1" | grep -o 'sha256:[0-9a-f]*' | head -1
    }
    local both=no
    [ "$(opened server.log)" = "$composition" ] && [ "$(opened client.log)" = "$composition" ] && both=yes
    echo "followed: $1: both played 0.2.0: $both; followed $(grep -c '^followed ' "$out/follow.log")," \
        "current $(grep -c '^current ' "$out/follow.log");" \
        "said: $(tr '\n' ' ' <"$work/play-$1.txt")"
}
play first
play again
mv "$work/mirror" "$work/gone"
play unreachable
