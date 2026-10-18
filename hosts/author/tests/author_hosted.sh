#!/usr/bin/env bash
# A shared session (ADR-0085, D881): Runners' level hosted over QUIC to
# three collaborators, each admitted by a token with its grants. Ben joins
# and stays; Ada renames the ground and claims it; Ben hears that Ada
# changed the level and who is present, and his rename of the ground is
# refused naming her; a record of his naming Ada as its author is refused;
# Cara, who may only read, reads the level and is refused a change; a
# token the host never issued is refused; Ben's token taken out of the
# tokens file disconnects him at once; and the level holds Ada's change.
#
# Ada, granted cooking, cooks the level by a plain name into the host's
# cooking directory, in her own, and is refused one naming a path (D885).
#
# usage: author_hosted.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
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
for each in ada ben cara stranger; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '# collaborators\n%s ada read edit claim cook\n%s ben read edit\n%s cara read\n' \
    "$(cat "$work/ada.token")" "$(cat "$work/ben.token")" "$(cat "$work/cara.token")" >"$work/tokens"
ground=c5400cf8-4b07-4d16-84c2-951852c66b40
rename() {
    echo '{"kind":"authoring.apply","id":'"$1"',"scene":"level.scene"'"$3"',"request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"'$ground'","name":"'"$2"'"}]}}'
}

"$author" host "$game" "$work/root" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" --cooks "$work/cooks" --cook "$cook" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
# Waits until a collaborator's output, or a file, holds a line matching $2.
heard() {
    for _ in $(seq 600); do
        grep -q "$2" "$work/$1" 2>/dev/null && return 0
        sleep 0.05
    done
    return 1
}
join() {
    "$author" join "127.0.0.1:$port" "$work/pin" "$work/$1.token"
}

# Ben joins and stays; each collaborator's records wait on what the other
# was answered, so the order is the host's to keep, not the scheduler's.
{
    echo '{"kind":"authoring.presence","id":1}'
    heard ada.out '"id":2,"answer"'
    # Presence is said at most ten times a second, so Ben asks until he
    # has heard Ada's claim.
    for each in $(seq 10 300); do
        grep -q '"claims":\[{"document":"level.scene"' "$work/ben.out" && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.05
    done
    rename 2 "bens ground" ""
    rename 3 "bens ground" ',"author":"ada"'
    heard marks revoked
    echo '{"kind":"authoring.presence","id":4}'
} | join ben >"$work/ben.out" 2>&1 &
benning=$!
{
    heard ben.out '"id":1,"answer"'
    rename 1 "adas ground" ""
    heard ada.out '"id":1,"answer"'
    echo '{"kind":"authoring.claim","id":2,"scene":"level.scene","entity":"'$ground'"}'
    heard ben.out 'the host closed'
    echo '{"kind":"authoring.cook","id":4,"output":"../escaped","cache":null}'
    echo '{"kind":"authoring.cook","id":5,"output":"content","cache":"cache"}'
    echo '{"kind":"authoring.end","id":3}'
} | join ada >"$work/ada.out" 2>&1 &
adaing=$!
touch "$work/marks"
heard ben.out '"id":3,'
grep -q '"answer":{"kind":"authoring.welcome","surfaceGeneration":1,"author":"ben","grants":\["read","edit"\]}' "$work/ben.out"
grep -q '"id":2,"answer":{"kind":"authoring.claimed","author":"ada"' "$work/ada.out"
# Ben heard what Ada did before his next reply, and was refused her ground.
grep -q '{"kind":"authoring.changed","author":"ada","document":"level.scene","digest":"sha256:' "$work/ben.out"
grep -q '"kind":"authoring.presence","authors":\[.*"author":"ada".*"claims":\[{"document":"level.scene","entity":"'$ground'"}\]' "$work/ben.out"
grep -q '"id":2,.*"code":"conflict",.*"author":"ada"' "$work/ben.out"
grep -q '"id":3,"error":{"code":"capability_denied",.*"message":"a record is its sender'"'"'s own"' "$work/ben.out"

{
    echo '{"kind":"authoring.read","id":1,"scene":"level.scene","queries":{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"scene.list_entities"}]}}'
    rename 2 "caras ground" ""
    echo '{"kind":"authoring.cook","id":3,"output":"/tmp/elsewhere","cache":null}'
    # The host fails closed: a kind it gives no grant to never reaches the
    # session, nor does a line that is no record.
    echo '{"kind":"authoring.unheard_of","id":4}'
    echo 'not a record'
} | join cara >"$work/cara.out" 2>&1 || true
grep -q '"id":1,"answer":' "$work/cara.out"
grep -q '"id":2,"error":{"code":"capability_denied",.*does not grant it' "$work/cara.out"
grep -q '"id":3,"error":{"code":"capability_denied"' "$work/cara.out"
grep -q '"id":4,"error":{"code":"unsupported_operation",.*"the host has no such record"' "$work/cara.out"
grep -q '"error":{"code":"validation_failed",.*"a record is one JSON object a line"' "$work/cara.out"
if join stranger </dev/null >"$work/stranger.out" 2>&1; then
    echo "a token the host never issued was welcomed"
    exit 1
fi
grep -q '"code":"capability_denied",.*"the token is not one the host'"'"'s owner issued"' "$work/stranger.out"

# Ben's token taken out: he is told and let go before his next record.
grep -v ' ben ' "$work/tokens" >"$work/tokens.next"
mv "$work/tokens.next" "$work/tokens"
sleep 2
echo revoked >"$work/marks"
wait "$benning" || true
wait "$adaing" || true
grep -q '"kind":"authoring.revoked","author":"ben"' "$work/ben.out"
grep -q 'the host closed' "$work/ben.out"
grep -q '"id":3,"answer":{"kind":"authoring.ended"}' "$work/ada.out"
grep -q '"id":4,"error":{"code":"capability_denied",.*plain names' "$work/ada.out"
grep -q '{"kind":"authoring.progress","id":5,' "$work/ada.out"
grep -q '"id":5,"answer":{"kind":"authoring.cooked"' "$work/ada.out"
[ -s "$work/cooks/ada/content/cook.receipt" ]
[ ! -e "$work/escaped" ]
grep -q '"adas ground"' "$work/root/level.scene"
if grep -q '"bens ground"\|"caras ground"' "$work/root/level.scene"; then
    echo "a refused change reached the level"
    exit 1
fi
echo "three collaborators shared one hosted session"
