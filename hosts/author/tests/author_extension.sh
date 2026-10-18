#!/usr/bin/env bash
# A Studio extension run headless (D874): a Kest program under a copy of
# Ledges' `extensions/` listed with its commands and what its doors are of;
# one command raising the chosen points, read and staged through its
# doors, applied as one step and undone; another making a marker entity
# with a component and a field; and an extension asking for a door it is
# not offered, listed as refused, with nothing it can run.
#
# usage: author_extension.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$3")"
trap 'echo "author_extension: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work"
cp -R "$repository/games/ledges" "$work/ledges"
mkdir -p "$work/ledges/extensions"
cat >"$work/ledges/extensions/lift.kest" <<'KEST'
module lift

import rawframe.studio

fn extend() {
    studio.command("Raise chosen", "raise")
    studio.command("Mark above", "mark")
}

// Each chosen entity a meter higher.
fn raise() {
    let i = u32(0)
    while i < studio.chosen() {
        let y = studio.number(i, "rawframe.physics2d.pose", "y")
        studio.set_number(i, "rawframe.physics2d.pose", "y", y + 1.0)
        i = i + u32(1)
    }
}

// A marker two meters over the first chosen entity.
fn mark() {
    let x = studio.number(u32(0), "rawframe.physics2d.pose", "x")
    let y = studio.number(u32(0), "rawframe.physics2d.pose", "y")
    let made = studio.make("marker")
    studio.add(made, "rawframe.physics2d.pose")
    studio.set_made(made, "rawframe.physics2d.pose", "x", x)
    studio.set_made(made, "rawframe.physics2d.pose", "y", y + 2.0)
}
KEST
cat >"$work/ledges/extensions/reach.kest" <<'KEST'
module reach

import rawframe.random

fn extend() {
    let roll = random.below(0, u32(6))
}
KEST
cp "$work/ledges/lift.scene" "$work/original.scene"
game="$work/ledges/ledges.game"
point0=a92a40f1-8d7d-4ca6-a834-d7159f93fcce
point1=cb16d95c-0c92-4521-8be9-befb0cdb5954
reply() { grep "\"id\":$1," "$work/replies"; }

# Listed, then the chosen points raised, then a marker made over the
# first, each one step written to the scene.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.extensions","id":2}'
    echo '{"kind":"authoring.extension","id":3,"scene":"lift.scene","extension":"extensions/lift.kest","command":"Raise chosen","entities":["'$point0'","'$point1'"]}'
    echo '{"kind":"authoring.extension","id":4,"scene":"lift.scene","extension":"extensions/lift.kest","command":"Mark above","entities":["'$point0'"]}'
} | "$author" session "$game" >"$work/replies" || true
reply 2 | grep -q '{"path":"extensions/lift.kest","commands":\["Raise chosen","Mark above"\],"capabilities":\["Studio","Edit"\]}'
reply 2 | grep -q '"path":"extensions/reach.kest","commands":\[\],.*"refused":"the extension does not start'
reply 3 | grep -q '"written":true,.*"undoable":1,'
reply 4 | grep -q '"written":true,.*"undoable":2,'
grep -A9 '"name": "lift point 0"' "$work/ledges/lift.scene" | grep -q '"y": 15'
grep -A10 '"name": "lift point 1"' "$work/ledges/lift.scene" | grep -q '"y": 15'
grep -A6 '"name": "marker"' "$work/ledges/lift.scene" | grep -q '"y": 17'

# Undone back to the scene's very bytes; a command the extension does not
# offer, and one of an extension that does not start, refused.
cp "$work/original.scene" "$work/ledges/lift.scene"
rm -f "$work/replies"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.extension","id":2,"scene":"lift.scene","extension":"extensions/lift.kest","command":"Mark above","entities":["'$point0'"]}'
    echo '{"kind":"authoring.undo","id":3,"scene":"lift.scene"}'
    echo '{"kind":"authoring.extension","id":4,"scene":"lift.scene","extension":"extensions/lift.kest","command":"Fly","entities":[]}'
    echo '{"kind":"authoring.extension","id":5,"scene":"lift.scene","extension":"extensions/reach.kest","command":"Any","entities":[]}'
    echo '{"kind":"authoring.extension","id":6,"scene":"lift.scene","extension":"../ledges.kest","command":"Any","entities":[]}'
} | "$author" session "$game" >"$work/replies" || true
reply 2 | grep -q '"written":true,'
reply 3 | grep -q '"undoable":0,'
cmp "$work/original.scene" "$work/ledges/lift.scene"
reply 4 | grep -q '"code":"target_not_found"'
reply 5 | grep -q 'does not start: the program asks for a door this table does not hold'
reply 6 | grep -q '"code":"target_not_found"'
echo "ran an extension's commands in a session, its doors its only reach"
