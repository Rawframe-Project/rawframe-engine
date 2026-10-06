#!/usr/bin/env bash
# A player's library updated from a publisher's mirror (SPEC-0038), as a
# launcher would: runners' cooked content is packed twice, as 0.1.0 and
# 0.2.0, each signed and added to the mirror with its Composition. The
# player's library, the publisher's key set pinned, is updated to 0.1.0 and
# plays from it; updated to 0.2.0, whose resources are the same, it fetches
# no blob; rolled back, it plays 0.1.0 again with the mirror gone; a blob
# that rots is healed from the mirror; and collect removes nothing either
# kept Composition needs. Then the same mirror, served over HTTPS by a web
# server whose certificate a test authority signed (D414), updates a second
# library to the same blobs, once that authority is trusted and not before.
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
content.composition = $(active)
content.library = $player
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 2
bots.endpoint = arena
CONF
    (cd "$work/elsewhere" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
    grep -q '"code":"composition_opened"' "$work/log.ndjson"
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
"$python" - "$mirror" "$keys" "$work/port" <<'SERVE' &
import functools, http.server, os, ssl, sys
root, keys, port = sys.argv[1:4]
class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *arguments):
        pass
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=root))
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(os.path.join(keys, "server.pem"), os.path.join(keys, "server.key"))
server.socket = context.wrap_socket(server.socket, server_side=True)
with open(port + ".part", "w") as written:
    written.write(str(server.server_address[1]))
os.replace(port + ".part", port)
server.serve_forever()
SERVE
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [ -s "$work/port" ] && break
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
echo "runners updated, rolled back, healed, and played from the player's library, and fetched over HTTPS"
