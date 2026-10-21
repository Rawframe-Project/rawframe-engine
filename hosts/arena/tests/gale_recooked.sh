#!/usr/bin/env bash
# Gale's materials followed while its sources are recooked (D734): the rock,
# cooked first as it ships, is recooked rougher once the client has read
# the materials' textures; the running client publishes the changed
# manifest and its scene reads the game's materials again, the program
# materials whose revisions stayed kept. The shore is recooked to sample
# a texture the game declares and nothing sampled before, which the scene
# then reads (D758). A machine without the shader
# toolchain, which Gale's sea needs (D484), skips, unless
# RAWFRAME_REQUIRE_SHADER_TOOLCHAIN is set (D701).
#
# usage: gale_recooked.sh <rawframe-arena> <rawframe-cook> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work/sources"
trap 'echo "gale_recooked: failed at line $LINENO"; grep -o "\"code\":\"stopped\".*" "$work/log.ndjson" 2>/dev/null | cut -c1-600' ERR
cp -r "$repository/games/gale/." "$work/sources/"
# A texture the game declares and no material samples at first: the
# recooked shore samples it, and the scene reads it then (D758).
cp "$work/sources/stone.png" "$work/sources/sand.png"
sed 's/bdade485c2e3a616d47796f633ec9154/5a2d7e1b9c4f40aa8e6b31d0c7f29e54/' "$work/sources/stone.png.rfmeta" \
    >"$work/sources/sand.png.rfmeta"
sed 's/^texture ea9d135c1c4e2805 stone.png$/&\ntexture 5a2d7e1b9c4f40aa sand.png/' "$repository/games/gale/gale.game" \
    >"$work/sources/gale.game"
grep -q 'sand.png' "$work/sources/gale.game"
if ! "$cook" "$work/sources" "$work/content" "$work/cache" >"$work/cooked.txt" 2>&1; then
    if grep -q "the shader toolchain builds, and the cook was given none" "$work/cooked.txt" &&
        [ -z "${RAWFRAME_REQUIRE_SHADER_TOOLCHAIN:-}" ]; then
        echo "skip: no shader toolchain"
        exit 0
    fi
    cat "$work/cooked.txt"
    exit 1
fi

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 14400
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $work/sources/gale.game
content.root = $work/content
content.reload_every = 12
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 2
bots.endpoint = arena
# What is checked is what is drawn, not how fast a loaded machine ticks
# (D516, D569).
world.degraded_ms = 600000
world.overload_ms = 0
CONF

(cd "$repository" && exec "$arena" --config "$work/arena.conf" >"$work/log.ndjson") &
running=$!

# Recook only once the scene read its materials from the first cook, which
# its textures being read follows. A recook of a program material takes
# long on a loaded machine, so the run lasts until the reload is seen, or a
# minute and a half after the recook, then is stopped as a user would.
for _ in $(seq 1 1200); do
    grep -q '"code":"textures_read"' "$work/log.ndjson" 2>/dev/null && break
    sleep 0.05
done
sed 's/"specular_roughness": 0.9/"specular_roughness": 0.5/' "$work/sources/rock.rfmaterial" >"$work/rock.rfmaterial"
mv "$work/rock.rfmaterial" "$work/sources/rock.rfmaterial"
grep -q '"specular_roughness": 0.5' "$work/sources/rock.rfmaterial"
sed 's/^  "overrides": {$/&\n    "stone": "5a2d7e1b9c4f40aa",/' "$work/sources/shore.rfmaterial" >"$work/shore.rfmaterial"
mv "$work/shore.rfmaterial" "$work/sources/shore.rfmaterial"
grep -q '"stone": "5a2d7e1b9c4f40aa"' "$work/sources/shore.rfmaterial"
"$cook" "$work/sources" "$work/content" "$work/cache" >/dev/null
for _ in $(seq 1 1800); do
    grep -q '"code":"materials_reloaded"' "$work/log.ndjson" && break
    sleep 0.05
done
kill -TERM "$running" 2>/dev/null || true
# Git's bash on Windows ends a native program it signals instead of asking
# it to stop, so only elsewhere is the arena's exit its own clean stop.
if ! wait "$running" && [ "$(uname -o)" != Msys ]; then
    exit 1
fi
grep -q '"code":"content_reloaded"' "$work/log.ndjson"
grep -o '"code":"materials_reloaded".*' "$work/log.ndjson" | head -1
grep -o '"code":"texture_followed".*' "$work/log.ndjson" | head -1
if grep -q '"code":"material_unread"' "$work/log.ndjson"; then
    exit 1
fi
