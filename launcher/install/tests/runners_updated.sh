#!/usr/bin/env bash
# A player's library updated from a publisher's mirror (SPEC-0038), as a
# launcher would, and played from the library alone, which plays the
# Composition it has active (D434): runners' cooked content is packed
# twice, as 0.1.0 and 0.2.0, each signed and added to the mirror with its
# Composition. The player's library, the publisher's key set pinned, is
# updated to 0.1.0 and plays from it; updated to 0.2.0, whose resources are the same, it fetches
# no blob; rolled back, it plays 0.1.0 again with the mirror gone; a blob
# that rots is healed from the mirror; and collect removes nothing either
# kept Composition needs. Then the same mirror, served over HTTPS by a web
# server whose certificate a test authority signed (D414), updates a second
# library to the same blobs, once that authority is trusted and not before.
# Last, the publisher releases each on the stable channel (SPEC-0020, D424),
# and a third library follows it: through a release, past a replayed
# pointer and one signed by a key it does not pin, both refused, to a
# rollback, after which it plays.
#
# usage: runners_updated.sh <rawframe-install> <rawframe-build> <rawframe-arena> <repository> <cooked content> <work directory>
set -euo pipefail

install=$1
build=$2
arena=$3
repository=$4
cooked=$5
work=$6

rm -rf "$work"
mkdir -p "$work/elsewhere"
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/runners/runners.game.rfmeta")
mirror=$work/mirror
player=$work/player
kid=$("$build" key rawframe "$work/keys" | cut -d' ' -f2)
mkdir -p "$mirror/keys" "$player/keys"
cp "$work/keys/rawframe.keys" "$mirror/keys/"
cp "$work/keys/rawframe.keys" "$player/keys/"

publish() {
    "$build" "$cooked" "$work/build-$1" rawframe/runners "$1" linux x86_64 client build.development tool \
        "$work/keys/$kid.key" >/dev/null
    local root
    root=$("$build" install "$work/build-$1" "$mirror" | cut -d' ' -f2)
    "$build" compose "$mirror" "$root" tool "$work/$1.composition" >/dev/null
}
publish 0.1.0
publish 0.2.0

active() {
    "$install" status "$player" | awk '$1 == "active" { print $3 }'
}

play() {
    cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 240
host.iteration_rate = 120
world.tick_rate = 60
kest.game_resource = $game
content.library = $player
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 2
bots.endpoint = arena
CONF
    (cd "$work/elsewhere" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
    # The library alone names the Composition: the one active (D434).
    grep -q "\"code\":\"composition_opened\".*\"composition\":\"sha256:$(basename "$(active)")\"" "$work/log.ndjson"
}

"$install" update "$player" "$work/0.1.0.composition" "$mirror" | tee "$work/first.txt"
grep -q 'fetched [1-9][0-9]* blobs' "$work/first.txt"
play

# The same resources under another version: a new Build, no new blob.
"$install" update "$player" "$work/0.2.0.composition" "$mirror" | tee "$work/second.txt"
grep -q 'fetched 0 blobs' "$work/second.txt"
test "$("$install" status "$player" | grep -c '^retained ')" -eq 1

# Back to 0.1.0 with nothing to fetch from.
mv "$mirror" "$work/away"
"$install" rollback "$player"
play
mv "$work/away" "$mirror"

# A blob that rots is fetched again.
rotten=$(find "$player/sha256" -type f | sort | head -n 1)
printf 'rotted' >"$rotten"
"$install" heal "$player" "$mirror" | tee "$work/healed.txt"
grep -q 'fetched 1 blobs, [0-9]* bytes, healed 1' "$work/healed.txt"
"$install" collect "$player" | tee "$work/collected.txt"
grep -q 'collected 0 blobs' "$work/collected.txt"
play

# Over HTTPS (D414): a test authority signs a certificate for localhost and
# 127.0.0.1, and a web server serves the mirror with it.
keys=$work/authority
mkdir -p "$keys"
# Git Bash would read the subjects as paths.
export MSYS2_ARG_CONV_EXCL='/CN='
openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
    -subj /CN=rawframe-test-authority -keyout "$keys/authority.key" -out "$keys/authority.pem" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj /CN=localhost \
    -keyout "$keys/server.key" -out "$keys/server.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' >"$keys/names.ext"
openssl x509 -req -in "$keys/server.csr" -CA "$keys/authority.pem" -CAkey "$keys/authority.key" \
    -CAcreateserial -days 2 -extfile "$keys/names.ext" -out "$keys/server.pem" 2>/dev/null
python=${PYTHON:-$(command -v python3 || command -v python)}
"$python" "$(dirname "$0")/serve_mirror.py" "$mirror" "$keys" "$work/port" 2>"$work/serve.txt" &
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for _ in $(seq 300); do
    [ -s "$work/port" ] && break
    if ! kill -0 "$server" 2>/dev/null; then
        cat "$work/serve.txt"
        echo "the mirror's web server ended before it listened"
        exit 1
    fi
    sleep 0.1
done
port=$(cat "$work/port")
fetched=$work/fetched
mkdir -p "$fetched/keys"
cp "$work/keys/rawframe.keys" "$fetched/keys/"

# Not trusting the test authority, the update is refused and changes nothing.
if "$install" update "$fetched" "$work/0.1.0.composition" "https://localhost:$port" 2>"$work/untrusted.txt"; then
    echo "an update from a server no trusted authority vouches for went through"
    exit 1
fi
grep -q 'not trusted for the host' "$work/untrusted.txt"
test -z "$("$install" status "$fetched")"

# Trusting it, the library fetches what the first did, by name and then by
# address, and holds the same blobs as the library updated from disk.
"$install" update "$fetched" "$work/0.1.0.composition" "https://localhost:$port/" \
    --authorities "$keys/authority.pem" | tee "$work/fetched.txt"
test "$(cut -d, -f1 "$work/fetched.txt")" = "$(cut -d, -f1 "$work/first.txt")"
"$install" update "$fetched" "$work/0.2.0.composition" "https://127.0.0.1:$port" \
    --authorities "$keys/authority.pem" | tee "$work/fetched-again.txt"
grep -q 'fetched 0 blobs' "$work/fetched-again.txt"
diff -r "$player/sha256" "$fetched/sha256"
player=$fetched
play

# Channels (SPEC-0020, D424): the publisher releases 0.1.0 on stable, and a
# third library follows it over HTTPS; then 0.2.0, which it follows with
# nothing to fetch.
over=("https://localhost:$port" --authorities "$keys/authority.pem")
"$build" release "$mirror" "$work/0.1.0.composition" 0.1.0 stable "$work/keys/$kid.key" | tee "$work/release-1.txt"
first_release=$(awk '{ print $2 }' "$work/release-1.txt")
followed=$work/followed
mkdir -p "$followed/keys"
cp "$work/keys/rawframe.keys" "$followed/keys/"
"$install" follow "$followed" rawframe/runners stable "${over[@]}" | tee "$work/follow-1.txt"
grep -q "followed rawframe/runners stable to 0.1.0, release $first_release, sequence 1" "$work/follow-1.txt"
mkdir -p "$work/replay"
cp "$mirror/channels/rawframe/runners/stable" "$mirror/channels/rawframe/runners/stable.sig" "$work/replay/"
"$build" release "$mirror" "$work/0.2.0.composition" 0.2.0 stable "$work/keys/$kid.key"
"$install" follow "$followed" rawframe/runners stable "${over[@]}" | tee "$work/follow-2.txt"
grep -q "to 0.2.0, .*sequence 2" "$work/follow-2.txt"
grep -q 'fetched 0 blobs' "$work/follow-2.txt"
# The first pointer served again is a replay, and changes nothing.
mkdir -p "$work/current"
cp "$mirror/channels/rawframe/runners/stable" "$mirror/channels/rawframe/runners/stable.sig" "$work/current/"
cp "$work/replay/stable" "$work/replay/stable.sig" "$mirror/channels/rawframe/runners/"
if "$install" follow "$followed" rawframe/runners stable "${over[@]}" 2>"$work/replayed.txt"; then
    echo "a replayed channel pointer was followed"
    exit 1
fi
grep -q 'a replay' "$work/replayed.txt"
cp "$work/current/stable" "$work/current/stable.sig" "$mirror/channels/rawframe/runners/"
# A pointer signed by a key the library does not pin is refused.
"$build" key rawframe "$work/other" >/dev/null
other=$(ls "$work/other"/*.key)
"$build" point "$mirror" rawframe/runners stable "$first_release" "$other" >/dev/null
if "$install" follow "$followed" rawframe/runners stable "${over[@]}" 2>"$work/unsigned.txt"; then
    echo "a channel pointer signed by an unknown key was followed"
    exit 1
fi
cp "$work/current/stable" "$work/current/stable.sig" "$mirror/channels/rawframe/runners/"
# A rollback: stable points back at 0.1.0 under a higher sequence.
"$build" point "$mirror" rawframe/runners stable "$first_release" "$work/keys/$kid.key"
"$install" follow "$followed" rawframe/runners stable "${over[@]}" | tee "$work/follow-3.txt"
grep -q "to 0.1.0, release $first_release, sequence 3" "$work/follow-3.txt"
player=$followed
play
echo "runners updated, rolled back, healed, and played from the player's library, fetched over HTTPS," \
    "and followed on a channel through a release, a replay, an unknown key, and a rollback"
