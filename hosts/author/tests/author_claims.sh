#!/usr/bin/env bash
# Presence and claims in one session (ADR-0085, D880), headless: Ada says
# what she has chosen and where she looks, and claims the ground; Ben's
# rename of it is refused naming her, his rename of a wall is kept, and his
# claim on the whole level is refused; the session's own change to the
# ground is refused too; a claim is known however its document is spelled;
# Ada's undo of the wall she renamed after Ben claimed it is refused; and
# when Ada leaves, her presence and claims are gone and Ben's change goes
# through. Nothing of it reaches the level's file but the changes kept.
#
# usage: author_claims.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3
trap 'echo "author_claims: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/runners/level.scene" "$work/root/level.scene"
game="$repository/games/runners/runners.game"
ground=c5400cf8-4b07-4d16-84c2-951852c66b40
wall=87e4430f-6ec1-4301-be72-ce8187494c34
east=0d077e42-123c-4358-b7e3-02e08c02e876
rename() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"'"$1"'","name":"'"$2"'"}]}'
}
view='{"eye":[0,5,10],"target":[0,0,0],"fieldOfView":60}'
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.present","id":2,"author":"ada","scene":"level.scene","entities":["'$ground'"],"view":'"$view"'}'
    echo '{"kind":"authoring.claim","id":3,"author":"ada","scene":"level.scene","entity":"'$ground'"}'
    echo '{"kind":"authoring.apply","id":4,"author":"ben","scene":"level.scene","request":'"$(rename $ground "bens ground")"'}'
    echo '{"kind":"authoring.apply","id":5,"author":"ben","scene":"level.scene","request":'"$(rename $wall "bens wall")"'}'
    echo '{"kind":"authoring.claim","id":6,"author":"ben","scene":"level.scene","entity":null}'
    echo '{"kind":"authoring.apply","id":7,"scene":"./level.scene","request":'"$(rename $ground "the ground")"'}'
    echo '{"kind":"authoring.apply","id":8,"author":"ada","scene":"level.scene","request":'"$(rename $east "adas wall")"'}'
    echo '{"kind":"authoring.claim","id":9,"author":"ben","scene":"level.scene","entity":"'$east'"}'
    echo '{"kind":"authoring.undo","id":10,"author":"ada","scene":"level.scene"}'
    echo '{"kind":"authoring.present","id":11,"author":"ben","scene":"level.scene","entities":[],"view":null}'
    echo '{"kind":"authoring.presence","id":12}'
    echo '{"kind":"authoring.leave","id":13,"author":"ada"}'
    echo '{"kind":"authoring.apply","id":14,"author":"ben","scene":"level.scene","request":'"$(rename $ground "bens ground")"'}'
    echo '{"kind":"authoring.presence","id":15}'
    echo '{"kind":"authoring.release","id":16,"author":"ada","scene":"level.scene","entity":"'$ground'"}'
    echo '{"kind":"authoring.claim","id":17,"scene":"level.scene","entity":null}'
    echo '{"kind":"authoring.claim","id":18,"author":"ben","scene":"elsewhere.scene","entity":null}'
    echo '{"kind":"authoring.end","id":19}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"kind":"authoring.presented"'
reply 3 | grep -q '"kind":"authoring.claimed","author":"ada","document":"level.scene","entity":"'$ground'"'
reply 4 | grep -q '"code":"conflict",.*"details":{"author":"ada","document":"level.scene"}'
reply 5 | grep -q '"written":true'
reply 6 | grep -q '"code":"conflict",.*"details":{"author":"ada"}'
reply 7 | grep -q '"code":"conflict",.*"author":"ada"'
reply 8 | grep -q '"written":true'
reply 9 | grep -q '"kind":"authoring.claimed"'
reply 10 | grep -q '"code":"conflict",.*"author":"ben"'
reply 12 | grep -q '"authors":\[{"author":"ada","document":"level.scene","chosen":\["'$ground'"\],"view":{"eye":\[0,5,10\],"target":\[0,0,0\],"fieldOfView":60},"claims":\[{"document":"level.scene","entity":"'$ground'"}\]},{"author":"ben","document":"level.scene","chosen":\[\],"view":null,"claims":\[{"document":"level.scene","entity":"'$east'"}\]}\]'
reply 13 | grep -q '"kind":"authoring.left","author":"ada"'
reply 14 | grep -q '"written":true'
reply 15 | grep -q '"authors":\[{"author":"ben",'
reply 16 | grep -q '"code":"target_not_found"'
reply 17 | grep -q '"code":"validation_failed"'
reply 18 | grep -q '"code":"target_not_found"'
grep -q '"bens ground"' "$work/root/level.scene"
grep -q '"bens wall"' "$work/root/level.scene"
grep -q '"adas wall"' "$work/root/level.scene"
if grep -q 'ada"\|claim' "$work/root/level.scene"; then
    echo "presence or a claim reached the level's file"
    exit 1
fi
echo "authors present and their claims kept in one session"
