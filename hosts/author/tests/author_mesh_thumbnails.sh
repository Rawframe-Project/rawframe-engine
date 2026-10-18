#!/usr/bin/env bash
# A mesh's and a material's thumbnails (D899, D900): the plaza cooked through a session, then its
# crate's thumbnail asked, which sets every mesh of the cook drawing by one
# run of the thumbnail tool beside the session, answered as being drawn
# until it is while other records are answered; the mound's then answered
# from what that run drew, without another. Each answer names the drawn picture beside the cook and
# the level that fits; each mesh covers some of its picture and not all of
# it. Says "skip: no device" where the tool found no adapter, unless
# RAWFRAME_REQUIRE_GPU is set, as the check sets it where lavapipe is.
#
# usage: author_mesh_thumbnails.sh <rawframe-author> <rawframe-cook> <rawframe-thumbnail> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
thumbnail=$3
repository=$4
work=$5
trap 'echo "author_mesh_thumbnails: failed at line $LINENO"; grep -v "authoring.progress" "$work/replies" 2>/dev/null | cut -c1-600
      cat "$work/content/thumbnails/thumbnails.log" 2>/dev/null' ERR
rm -rf "$work"
mkdir -p "$work"
game="$repository/games/plaza/plaza.game"
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.cook","id":2,"output":"'"$work/content"'","cache":null}'
    # Four minutes only bounds a cook on a sanitized tree under load (D507).
    for _ in $(seq 2400); do
        grep -q '"id":2,"answer"\|"id":2,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    # The crate asked until it is drawn, which the session answers as being
    # drawn meanwhile, never waiting for it; three minutes only bound a
    # loaded machine's software rasterizer. Then the figure and the mound
    # (at 32 pixels) until each is drawn too, the run drawing its meshes one
    # after another, before the session's end stops the run.
    for asset in crate.gltf figure.gltf mound.gltf paving.rfmaterial; do
        size=64
        [ "$asset" = mound.gltf ] && size=32
        for each in $(seq 100 1900); do
            echo '{"kind":"authoring.thumbnail","id":"'"$asset$each"'","asset":"'"$asset"'","size":'"$size"'}'
            echo '{"kind":"authoring.describe","id":"between'"$asset$each"'"}'
            grep -q '"answer":{"kind":"authoring.thumbnail","asset":"'"$asset"'"\|"class":"unsupported"' \
                "$work/replies" && break
            sleep 0.1
        done
    done
    echo '{"kind":"authoring.thumbnail","id":5,"asset":"gate.scene","size":64}'
    echo '{"kind":"authoring.end","id":6}'
} | "$author" session "$game" --cook "$cook" --thumbnail "$thumbnail" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.cooked"'
if grep -q "skip: no device" "$work/content/thumbnails/thumbnails.log" 2>/dev/null && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
# The first asking answered as being drawn, and a describe between the
# askings answered while the tool drew.
grep '"id":"crate.gltf100",' "$work/replies" | grep -q '"class":"unavailable"'
grep -q '"id":"betweencrate.gltf100","answer"' "$work/replies"
grep -q '"answer":{"kind":"authoring.thumbnail","asset":"crate.gltf","object":"[^"]*/content/thumbnails/[0-9a-f]\{64\}.texture","level":2,"width":64,"height":64}' "$work/replies"
grep -q '"answer":{"kind":"authoring.thumbnail","asset":"mound.gltf","object":"[^"]*/content/thumbnails/[0-9a-f]\{64\}.texture","level":3,"width":32,"height":32}' "$work/replies"
reply 5 | grep -q '"code":"unsupported_operation"'
# The paving, a material sampling its picture, on the engine's ball (D900).
grep -q '"answer":{"kind":"authoring.thumbnail","asset":"paving.rfmaterial","object":"[^"]*/content/thumbnails/[0-9a-f]\{64\}.texture","level":2,"width":64,"height":64}' "$work/replies"
# One run drew them all, the plaza's three meshes and seven materials (its
# crate's two among them), each covering some of its picture and not all of
# it.
[[ $(grep -c '^thumbnail ' "$work/content/thumbnails/thumbnails.log") -eq 10 ]]
if grep '^thumbnail ' "$work/content/thumbnails/thumbnails.log" | grep -q 'covering \(0\.0[0-4]\|1\.00\)'; then
    echo "a mesh covered too little or all of its picture"
    exit 1
fi
echo "drew the plaza's meshes and materials as thumbnails through a session"
