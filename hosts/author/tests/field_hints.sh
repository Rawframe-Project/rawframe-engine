#!/usr/bin/env bash
# What authors are told of a game's fields (D783): the starter template's
# `field` lines and the engine's own hints in `rawframe-author describe`, a
# line that does not read refused at its line, and one naming a field its
# component does not have, or a color on a real number, refused by name.
# And an emitter of Plaza's hinted as model.kest measures it (D831).
# Run from the repository root.
#
#   field_hints.sh <rawframe-author>
set -euo pipefail

author="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$author" new templates/starter-3d "$work/hinted" >/dev/null
"$author" describe "$work/hinted/hinted.game" >"$work/described"
python3 - "$work/described" <<'PY'
import json, sys
described = json.load(open(sys.argv[1]))
fields = {(c["name"], f["name"]): f for c in described["components"] for f in c["fields"]}
side = fields[("starter.ground", "side")]
assert side["least"] == 4 and side["most"] == 100 and side["unit"] == "m", side
assert fields[("starter.mover", "x")]["unit"] == "m"
assert fields[("starter.look", "color")].get("color") is True
assert "unit" not in fields[("starter.look", "scaleX")]
assert fields[("starter.look", "mesh")]["asset"] == "mesh"
print("hinted: side 4 to 100 m, a box's look a color, its mesh an asset")
PY
# An emitter's fields as model.kest measures them, its colors colors, and
# its material an asset (D831): Plaza's fountain.
"$author" describe games/plaza/plaza.game >"$work/described"
python3 - "$work/described" <<'PY'
import json, sys
described = json.load(open(sys.argv[1]))
fields = {(c["name"], f["name"]): f for c in described["components"] for f in c["fields"]}
fountain = lambda name: fields[("plaza.fountain", name)]
assert fountain("colorStart").get("color") is True and fountain("colorEnd").get("color") is True
assert fountain("spread")["unit"] == "rad" and fountain("spread")["least"] == 0 and fountain("spread")["most"] > 3.14
assert fountain("lifetime")["unit"] == "s" and fountain("variation")["most"] == 1
assert fountain("material")["asset"] == "material" and "unit" not in fountain("seed")
print("an emitter hinted: its colors, its spread 0 to pi rad, its material an asset")
PY
# The game names its layers on the engine's layer field (D788).
cp -R "$work/hinted" "$work/layered"
printf "%s\n" "field rawframe.world.layer layer labels 1 ground 2 props" >>"$work/layered/hinted.game"
"$author" describe "$work/layered/hinted.game" >"$work/described"
python3 - "$work/described" <<'PY'
import json, sys
described = json.load(open(sys.argv[1]))
layer = next(c for c in described["components"] if c["name"] == "rawframe.world.layer")
labels = layer["fields"][0]["labels"]
assert [l["name"] for l in labels] == ["ground", "props"], labels
print("layers named: ground, props")
PY
cp -R "$work/hinted" "$work/body"
printf "%s\n" "field starter.ground side range 10 2" >>"$work/body/hinted.game"
if "$author" describe "$work/body/hinted.game" >"$work/refused"; then
    echo "a range whose least is past its most read"
    exit 1
fi
grep -q 'least first' "$work/refused" && echo "refused: a range backwards"
cp -R "$work/hinted" "$work/missing"
printf '%s\n' "field starter.ground width unit m" >>"$work/missing/hinted.game"
if "$author" describe "$work/missing/hinted.game" >"$work/refused"; then
    echo "a hint for a field the component lacks read"
    exit 1
fi
grep -q '"field": "width"' "$work/refused" && echo "refused: a field the component does not have"
cp -R "$work/hinted" "$work/real"
printf '%s\n' "field starter.ground side color" >>"$work/real/hinted.game"
if "$author" describe "$work/real/hinted.game" >"$work/refused"; then
    echo "a color on a real number read"
    exit 1
fi
grep -q '"field": "side"' "$work/refused" && echo "refused: a color that is not an unsigned number"
cp -R "$work/hinted" "$work/kind"
printf '%s\n' "field starter.ground side asset picture" >>"$work/kind/hinted.game"
if "$author" describe "$work/kind/hinted.game" >"$work/refused"; then
    echo "an asset of no kind read"
    exit 1
fi
grep -q 'texture, mesh, material' "$work/refused" && echo "refused: an asset of no kind there is"
