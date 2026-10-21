#!/usr/bin/env bash
# A game kept under a folder whose name no single ANSI code page holds,
# as a Windows profile named in Turkish or Japanese puts one: the cook
# reads its sources and writes its content there. On Windows a program's
# narrow strings are UTF-8 only by its manifest (D956); without it the
# name reached the cook with '?' for each such character.
#
# usage: unicode_path.sh <rawframe-cook> <repository> <work directory>
set -euo pipefail

cook=$1
repository=$2
work=$3

rm -rf "$work"
named="$work/Sıraç ğüş 日本"
mkdir -p "$named/sources"
cp "$repository/games/plaza/paving.png" "$repository/games/plaza/paving.png.rfmeta" \
    "$repository/games/plaza/paving.rfmaterial" "$repository/games/plaza/paving.rfmaterial.rfmeta" "$named/sources/"
"$cook" "$named/sources" "$named/content" >"$work/cook.log" 2>&1 || {
    cat "$work/cook.log"
    exit 1
}
grep -q "cooked 2, reused 0, failed 0" "$work/cook.log"
test -f "$named/content/content.manifest"
rm -rf "$work"
echo "cooked under a named folder"
