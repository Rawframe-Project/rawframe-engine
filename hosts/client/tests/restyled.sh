#!/usr/bin/env bash
# A game's styles edited while it plays (D634): a copy of the game is
# played with its styles read again, and once the client's player is in,
# a color in them is changed as an author would; the client styles its UI
# anew. Run as clicked.sh is.
#
# usage: restyled.sh <rawframe-server> <rawframe-client> <client settings>
#                    <game directory> <game file> <styles file> <text> <text>
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp -r "$4" "$work/game"
cp "$3" "$work/client.conf"
printf 'ui.reload_every = 30\n' >>"$work/client.conf"
bash "$here/clicked.sh" "$1" "$2" "$work/client.conf" "$work/game/$5" \
    "edit=$work/game/$6:$7:$8" "until=ui_restyled"
