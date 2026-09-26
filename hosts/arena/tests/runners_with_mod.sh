#!/usr/bin/env bash
# Runners played with mods from a Composition (D180, D181): the game and
# the runners-timers and runners-sting mods are cooked, packed, and signed
# apart, and composed with them as Mods. Runners takes them under its open
# policy: the two timers join the World beside the game's own entities, and
# sting's handler, on its own untrusted machine, counts every hit a second
# time, so each score kept has an even count of hits taken. A mod whose range
# the game's Mod API version is outside, and one whose timer is also
# persistent, are refused before any World runs. A checkpoint taken with a
# mod is refused without it, naming the mod removed (D197).
#
# usage: runners_with_mod.sh <rawframe-arena> <rawframe-cook> <rawframe-build> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
build=$3
repository=$4
work=$5

rm -rf "$work"
mkdir -p "$work/game" "$work/mod" "$work/sting" "$work/penalty" "$work/lenient" "$work/swift" "$work/late" "$work/wide" "$work/elsewhere"
cp -r "$repository/games/runners/." "$work/game/"
cp -r "$repository/games/runners-timers/." "$work/mod/"
cp -r "$repository/games/runners-sting/." "$work/sting/"
cp -r "$repository/games/runners-penalty/." "$work/penalty/"
# A second provider of the penalty, which costs a hit one: runners prefers
# runners-penalty's.
cp -r "$repository/games/runners-penalty/." "$work/lenient/"
perl -pi -e 's/values\[0\].taken \* 2/values[0].taken/' "$work/lenient/penalty.kest"
# Resources of its own: a Composition names each resource once.
perl -pi -e 's/"resourceId": "[0-9a-f]*"/"resourceId": "1e41e470000000000000000000000001"/' "$work/lenient/kest.project.rfmeta"
perl -pi -e 's/"resourceId": "[0-9a-f]*"/"resourceId": "1e41e470000000000000000000000002"/' "$work/lenient/penalty.mod.rfmeta"
cp -r "$repository/games/runners-swift/." "$work/swift/"
cp -r "$repository/games/runners-timers/." "$work/late/"
perl -pi -e 's/^modapi 1$/modapi >=2/' "$work/late/timers.mod"
cp -r "$repository/games/runners-timers/." "$work/wide/"
cat >"$work/wide/timers.scene" <<'SCENE'
{
  "kind": "rawframe.scene",
  "formatVersion": 1,
  "schema": {
    "rawframe.world.persistent": "afc8322918a647ff",
    "runners.age": "b1e3cd3c575b7a33"
  },
  "entities": [
    {
      "id": "faadb25f-9d04-442e-bbd6-db5e371a6a89",
      "name": "dawn",
      "components": {
        "rawframe.world.persistent": {},
        "runners.age": {}
      }
    }
  ]
}
SCENE
"$cook" "$work/game" "$work/game-cooked" >/dev/null
"$cook" "$work/mod" "$work/mod-cooked" >/dev/null
"$cook" "$work/sting" "$work/sting-cooked" >/dev/null
"$cook" "$work/penalty" "$work/penalty-cooked" >/dev/null
"$cook" "$work/lenient" "$work/lenient-cooked" >/dev/null
"$cook" "$work/swift" "$work/swift-cooked" >/dev/null
"$cook" "$work/late" "$work/late-cooked" >/dev/null
"$cook" "$work/wide" "$work/wide-cooked" >/dev/null

library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
pack() {
    "$build" "$1" "$2" "$3" 0.1.0 linux x86_64 client build.development tool "$library/keys/$kid.key" >/dev/null
    "$build" install "$2" "$library" | cut -d' ' -f2
}
game_root=$(pack "$work/game-cooked" "$work/game-build" rawframe/runners)
mod_root=$(pack "$work/mod-cooked" "$work/mod-build" rawframe/runners-timers)
sting_root=$(pack "$work/sting-cooked" "$work/sting-build" rawframe/runners-sting)
penalty_root=$(pack "$work/penalty-cooked" "$work/penalty-build" rawframe/runners-penalty)
lenient_root=$(pack "$work/lenient-cooked" "$work/lenient-build" rawframe/runners-lenient)
swift_root=$(pack "$work/swift-cooked" "$work/swift-build" rawframe/runners-swift)
late_root=$(pack "$work/late-cooked" "$work/late-build" rawframe/runners-late)
wide_root=$(pack "$work/wide-cooked" "$work/wide-build" rawframe/runners-wide)
"$build" compose "$library" "$game_root" tool "$work/plain.composition" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/modded.composition" --mod "$mod_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/stung.composition" --mod "$mod_root" --mod "$sting_root" \
    >/dev/null
"$build" compose "$library" "$game_root" tool "$work/penalized.composition" --mod "$penalty_root" --mod "$lenient_root" \
    >/dev/null
"$build" compose "$library" "$game_root" tool "$work/swift.composition" --mod "$swift_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/late.composition" --mod "$late_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/wide.composition" --mod "$wide_root" >/dev/null
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/runners/runners.game.rfmeta")

run() {
    cat >"$work/arena.conf" <<CONF
host.maximum_iterations = ${2:-30}
host.iteration_rate = 120
world.tick_rate = 60
kest.game_resource = $game
content.composition = $1
content.library = $library
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 8
bots.endpoint = arena
bots.session = runner
save.directory = $work/saves
save.namespace = 72756e6e657273000000000000000001
${3:-}
CONF
    (cd "$work/elsewhere" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
}
entities() {
    grep -o '"code":"game_loaded".*"entities":[0-9]*' "$work/log.ndjson" | head -1 | sed 's/.*"entities"://'
}

# Out of range: refused, and nothing starts.
if run "$work/late.composition"; then
    echo "a mod outside the game's Mod API version was taken" >&2
    exit 1
fi
grep -q "Mod API version is outside a mod's range" "$work/log.ndjson"
# A timer that is also persistent: refused, as a mod adds values only.
if run "$work/wide.composition"; then
    echo "a mod's entity holding more than the point's component was spawned" >&2
    exit 1
fi
grep -q "holds the point's component and nothing else" "$work/log.ndjson"

run "$work/plain.composition"
plain=$(entities)
run "$work/modded.composition" 30 "checkpoint.capture_ticks = 10
checkpoint.capture_prefix = $work/modded-"
modded=$(entities)
if [ "$modded" -ne $((plain + 2)) ]; then
    echo "the mod's two timers did not join the World: $plain without it, $modded with it" >&2
    exit 1
fi

# A checkpoint knows the mods it ran with (D197): restored without the mod,
# it is refused naming the mod removed; with it, it restores.
status=0
run "$work/plain.composition" 30 "checkpoint.restore = $work/modded-10.rfsn" || status=$?
if [ "$status" -ne 65 ]; then
    echo "a checkpoint taken with a mod, restored without it, exited $status, not 65 (an incompatible artifact)" >&2
    exit 1
fi
grep -q "another set of mods.*removed: rawframe/runners-timers@" "$work/log.ndjson"
run "$work/modded.composition" 30 "checkpoint.restore = $work/modded-10.rfsn"
grep -q '"code":"checkpoint_restored"' "$work/log.ndjson"

# Every score kept has taken hits in twos, and some runner was hit: the
# total taken, or a failure naming `$1`.
taken_in_twos() {
    local taken each total=0
    taken=$(python3 - "$work/saves" <<'PY'
import glob, struct, sys
taken = []
for path in sorted(glob.glob(sys.argv[1] + "/p-*.rfsave")):
    body = open(path, "rb").read()[:-32]
    # The score is the last sixteen bytes before the digest.
    taken.append(struct.unpack_from("<iiii", body, len(body) - 16)[3])
print(" ".join(map(str, taken)))
PY
)
    echo "taken: $taken" >&2
    for each in $taken; do
        if [ $((each % 2)) -ne 0 ]; then
            echo "a hit was counted once with $1" >&2
            exit 1
        fi
        total=$((total + each))
    done
    if [ "$total" -eq 0 ]; then
        echo "no runner was hit, so $1 proved nothing" >&2
        exit 1
    fi
    echo "$total"
}

# Stung: the handler is loaded, and every score kept has taken hits in twos.
rm -rf "$work/saves"
run "$work/stung.composition" 600
grep -q '"code":"game_loaded".*"modHandlers":1' "$work/log.ndjson"
stung=$(taken_in_twos "runners-sting's handler")

# Penalized: runners-penalty's provider is bound, the lenient one's claim
# is set aside by runners' preference (D202), and every hit costs two taken.
rm -rf "$work/saves"
run "$work/penalized.composition" 600
grep -q '"code":"game_loaded".*"modProviders":1,.*"modClaimsSetAside":1' "$work/log.ndjson"
penalized=$(taken_in_twos "runners-penalty's provider")

# Swift: the game's age system is replaced by the mod's, and the hall ages
# a thousand ticks a tick.
rm -rf "$work/saves"
run "$work/swift.composition" 60
grep -q '"code":"game_loaded".*"modReplacements":1' "$work/log.ndjson"
aged=$(python3 - "$work/saves/world.rfsave" <<'PY'
import struct, sys
body = open(sys.argv[1], "rb").read()[:-32]
# The hall's age is the last eight bytes before the digest.
print(struct.unpack_from("<Q", body, len(body) - 8)[0])
PY
)
if [ "$aged" -eq 0 ] || [ $((aged % 1000)) -ne 0 ]; then
    echo "the hall aged $aged ticks with runners-swift replacing its age system" >&2
    exit 1
fi
echo "mod taken: $plain entities without it, $modded with it; stung hits $stung; penalized hits $penalized; swift hall aged $aged; checkpoints know their mods"
