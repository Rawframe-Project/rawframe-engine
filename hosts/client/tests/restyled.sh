#!/usr/bin/env bash
# A game's styles (D634) or UI document (D619) edited while it plays: a
# copy of the game is played with them read again, and once the client's
# player is in, a value in one is changed as an author would; the client
# styles or arranges its UI anew, which it says as <event> (ui_restyled
# by default). Run as clicked.sh is.
#
# usage: restyled.sh <rawframe-server> <rawframe-client> <client settings>
#                    <game directory> <game file> <edited file> <text> <text>
#                    [<event>]
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp -r "$4" "$work/game"
cp "$3" "$work/client.conf"
printf 'ui.reload_every = 30\n' >>"$work/client.conf"
bash "$here/clicked.sh" "$1" "$2" "$work/client.conf" "$work/game/$5" \
    "edit=$work/game/$6:$7:$8" "until=${9:-ui_restyled}"
