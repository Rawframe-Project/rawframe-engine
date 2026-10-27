#!/usr/bin/env bash
# Claims on a UI document's nodes, headless,
# over a copy of Stalls' UI: Ada chooses the purse and claims it; Ben's
# change to the purse is refused naming her, and so are his removing the
# wallet that holds it and moving it out; his change to the wallet's own
# field and to the tip beside the purse are kept, and his claim on the whole
# document refused. Ada changes the purse; Ben claims the wallet, and Ada's
# next change to the purse and her undo are refused naming him, until he
# lets it go and her undo goes through. A node claimed in a style sheet, and
# a node chosen by an empty name, are refused. Nothing of it reaches the
# document but the changes kept.
#
# usage: author_node_claims.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3
trap 'echo "author_node_claims: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/tycoon/tycoon.ui" "$repository/games/tycoon/tycoon.styles" "$work/root/"
game="$repository/games/tycoon/tycoon.game"
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
field() {
    echo '{"operation":"ui.set_field","node":"'"$1"'","field":"'"$2"'","value":'"$3"'}'
}
apply() {
    echo '{"kind":"authoring.apply","id":'"$1"',"author":"'"$2"'","scene":"tycoon.ui","request":'"$(request "$3")"'}'
}
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.present","id":2,"author":"ada","scene":"tycoon.ui","nodes":["tycoon.purse"],"view":null}'
    echo '{"kind":"authoring.claim","id":3,"author":"ada","scene":"tycoon.ui","node":"tycoon.purse"}'
    apply 4 ben "$(field tycoon.purse fill '"#c01818c0"')"
    apply 5 ben '{"operation":"ui.remove_node","node":"tycoon.wallet"}'
    apply 6 ben '{"operation":"ui.move_node","node":"tycoon.purse","parent":null,"place":0}'
    apply 7 ben "$(field tycoon.wallet margin 3)"
    apply 8 ben "$(field tycoon.tip radius 5)"
    echo '{"kind":"authoring.claim","id":9,"author":"ben","scene":"tycoon.ui","entity":null}'
    apply 10 ada "$(field tycoon.purse fill '"#18c018c0"')"
    echo '{"kind":"authoring.claim","id":11,"author":"ben","scene":"tycoon.ui","node":"tycoon.wallet"}'
    apply 12 ada "$(field tycoon.purse radius 2)"
    echo '{"kind":"authoring.undo","id":13,"author":"ada","scene":"tycoon.ui"}'
    echo '{"kind":"authoring.presence","id":14}'
    echo '{"kind":"authoring.release","id":15,"author":"ben","scene":"tycoon.ui","node":"tycoon.wallet"}'
    echo '{"kind":"authoring.undo","id":16,"author":"ada","scene":"tycoon.ui"}'
    echo '{"kind":"authoring.claim","id":17,"author":"ben","scene":"tycoon.styles","node":"tycoon.purse"}'
    echo '{"kind":"authoring.present","id":18,"author":"ben","scene":"tycoon.ui","nodes":[""],"view":null}'
    echo '{"kind":"authoring.end","id":19}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"kind":"authoring.presented"'
reply 3 | grep -q '"kind":"authoring.claimed","author":"ada","document":"tycoon.ui","node":"tycoon.purse"'
for each in 4 5 6; do
    reply $each | grep -q '"code":"conflict",.*"details":{"author":"ada","document":"tycoon.ui"}'
done
reply 7 | grep -q '"written":true'
reply 8 | grep -q '"written":true'
reply 9 | grep -q '"code":"conflict",.*"details":{"author":"ada"}'
reply 10 | grep -q '"written":true'
reply 11 | grep -q '"kind":"authoring.claimed","author":"ben","document":"tycoon.ui","node":"tycoon.wallet"'
reply 12 | grep -q '"code":"conflict",.*"author":"ben"'
reply 13 | grep -q '"code":"conflict",.*"author":"ben"'
reply 14 | grep -q '"authors":\[{"author":"ada","document":"tycoon.ui","chosen":\["tycoon.purse"\],"view":null,"claims":\[{"document":"tycoon.ui","node":"tycoon.purse"}\]},{"author":"ben","claims":\[{"document":"tycoon.ui","node":"tycoon.wallet"}\]}\]'
reply 15 | grep -q '"kind":"authoring.released","author":"ben","document":"tycoon.ui","node":"tycoon.wallet"'
reply 16 | grep -q '"written":true'
reply 17 | grep -q '"code":"validation_failed"'
reply 18 | grep -q '"code":"validation_failed"'
# Ben's wallet margin and tip radius kept; Ada's fill taken back.
grep -q '#c01818c0\|#18c018c0' "$work/root/tycoon.ui" && {
    echo "a refused or undone fill reached the document"
    exit 1
}
python3 - "$work/root/tycoon.ui" <<'EOF'
import json, sys
nodes = json.load(open(sys.argv[1]))["nodes"]
wallet = next(each for each in nodes if each["component"] == "tycoon.wallet")
tip = next(each for each in wallet["children"] if each["component"] == "tycoon.tip")
assert wallet["margin"] == 3, wallet["margin"]
assert tip["radius"] == 5, tip["radius"]
EOF
if grep -q 'ada\|claim' "$work/root/tycoon.ui"; then
    echo "presence or a claim reached the document"
    exit 1
fi
echo "UI nodes claimed and kept from others in one session"
