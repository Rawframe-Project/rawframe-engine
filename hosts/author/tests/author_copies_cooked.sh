#!/usr/bin/env bash
# A cook made at a shared session's host brought to a collaborator's
# machine (D905): Ada, granted cooking, cooks Runners at the host by a
# plain name and brings it into a directory of her own with `copy-cooked`,
# which then holds the host's manifest and every file it locates, alike;
# brought again, nothing is read over; a file spoiled here is read over
# alone. A cook never made, a name that is a path, and a file outside the
# cook are refused, and Cara, who may only read, is refused the copy.
#
# usage: author_copies_cooked.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
rm -rf "$work"
mkdir -p "$work/root" "$work/cooks"
trap 'echo "failed at line $LINENO"; for each in "$work"/*.out "$work"/host.log; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
cp "$repository/games/runners/level.scene" "$work/root/level.scene"
game="$(native "$repository")/games/runners/runners.game"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
for each in ada cara; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '%s ada read cook\n%s cara read\n' "$(cat "$work/ada.token")" "$(cat "$work/cara.token")" >"$work/tokens"

"$author" host "$game" "$work/root" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" --cooks "$work/cooks" --cook "$cook" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
copy() {
    "$author" copy-cooked "127.0.0.1:$port" "$work/pin" "$work/$1.token" "$2" "$work/$3"
}

# A copy before any cook finds none.
if copy ada content here >"$work/early.out" 2>&1; then
    echo "a cook never made was copied"
    exit 1
fi
grep -q 'no cook in this session wrote there' "$work/early.out"
[ ! -e "$work/here" ]
{
    echo '{"kind":"authoring.cook","id":1,"output":"content","cache":"cache"}'
    echo '{"kind":"authoring.cooked_file","id":2,"output":"content","file":"../../tokens","offset":0}'
    echo '{"kind":"authoring.cooked_file","id":3,"output":"content","file":"content.manifest","offset":99999999}'
    echo '{"kind":"authoring.end","id":4}'
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ada.token" >"$work/ada.out" 2>&1 || true
grep -q '"id":1,"answer":{"kind":"authoring.cooked"' "$work/ada.out"
grep -q '"id":2,"error":{"code":"validation_failed"' "$work/ada.out"
grep -q '"id":3,"error":{"code":"validation_failed",.*within the file' "$work/ada.out"

copy ada content here >"$work/first.out"
grep -q '"kind":"copied","copied":[1-9][0-9]*,"kept":0,' "$work/first.out"
cmp "$work/cooks/ada/content/content.manifest" "$work/here/content.manifest"
for each in "$work"/cooks/ada/content/objects/*; do
    cmp "$each" "$work/here/objects/$(basename "$each")"
done
copy ada content here >"$work/again.out"
grep -q '"kind":"copied","copied":0,"kept":[1-9][0-9]*,"bytes":0}' "$work/again.out"
spoiled=$(ls "$work/here/objects" | head -1)
printf 'spoiled' >"$work/here/objects/$spoiled"
copy ada content here >"$work/mended.out"
grep -q '"kind":"copied","copied":1,' "$work/mended.out"
cmp "$work/cooks/ada/content/objects/$spoiled" "$work/here/objects/$spoiled"

if copy ada ../escaped there >"$work/path.out" 2>&1; then
    echo "a cook named by a path was copied"
    exit 1
fi
grep -q 'plain names' "$work/path.out"
if copy cara content there >"$work/cara.out" 2>&1; then
    echo "a collaborator without cooking copied a cook"
    exit 1
fi
grep -q 'does not grant it' "$work/cara.out"
[ ! -e "$work/there/content.manifest" ]
echo "a cook made at the host was brought here whole"
