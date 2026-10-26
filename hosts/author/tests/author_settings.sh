#!/usr/bin/env bash
# A source's import settings through a session: the plaza's paving
# read with the settings the texture importer offers; a setting at its
# fallback cleared from the sidecar and another written, the sidecar's
# identity kept; a value the importer refuses, a setting it does not offer,
# and a path that is no source refused, the sidecar left as it was; and the
# copy cooked with its new settings.
#
# usage: author_settings.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
rm -rf "$work"
mkdir -p "$work"
cp -R "$repository/games/plaza" "$work/game"
game="$work/game/plaza.game"
sidecar="$work/game/paving.png.rfmeta"
identity=$(grep -o '"resourceId": "[0-9a-f]*"' "$sidecar")

reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.import_settings","id":2,"asset":"paving.png"}'
    echo '{"kind":"authoring.set_import_settings","id":3,"asset":"paving.png","settings":{"exact":false,"color":"linear"}}'
    echo '{"kind":"authoring.set_import_settings","id":4,"asset":"paving.png","settings":{"color":"cmyk"}}'
    echo '{"kind":"authoring.set_import_settings","id":5,"asset":"paving.png","settings":{"sharpness":1}}'
    echo '{"kind":"authoring.set_import_settings","id":6,"asset":"paving.png","settings":{"exact":"yes"}}'
    echo '{"kind":"authoring.import_settings","id":7,"asset":"../plaza/paving.png"}'
    echo '{"kind":"authoring.import_settings","id":8,"asset":"kest.project"}'
    echo '{"kind":"authoring.end","id":9}'
} | "$author" session "$game" >"$work/replies" || true

reply 2 | grep -q '"answer":{"kind":"authoring.import_settings","asset":"paving.png","importer":"rawframe.texture","settings":{"exact":true},"fields":\[{"name":"color","type":"text","fallback":"srgb","choices":\["srgb","linear"\]},{"name":"exact","type":"truth","fallback":false},{"name":"levels","type":"truth","fallback":true}\]}'
# exact at its fallback cleared, color written; the identity kept.
reply 3 | grep -q '"settings":{"color":"linear"}'
grep -q '"color": "linear"' "$sidecar"
if grep -q '"exact"' "$sidecar"; then echo "exact at its fallback was kept"; exit 1; fi
grep -q "$identity" "$sidecar"
# Refused, each saying why; the sidecar as it was.
reply 4 | grep -q '"code":"validation_failed".*a texture'"'"'s color is srgb or linear'
reply 5 | grep -q '"code":"validation_failed".*"setting":"sharpness"'
reply 6 | grep -q '"code":"validation_failed".*not of its kind'
reply 7 | grep -q '"code":"target_not_found"'
# A Kest source's importer offers nothing; reading it says so.
reply 8 | grep -q '"importer":"rawframe.kest","settings":null,"fields":\[\]'
grep -q '"color": "linear"' "$sidecar"

# The copy cooks with the setting.
"$cook" "$work/game" "$work/content" >"$work/cook.out"
grep -q 'cooked ' "$work/cook.out"
echo "the paving's import settings read, set, refused as asked, and cooked"
