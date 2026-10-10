#!/usr/bin/env bash
# Exports a game for Android and plays the package made on a device or
# emulator, all of it there but the packaging: the Android build's
# rawframe-export runs on the device with its cook, build tool, and server
# beside it, for the device's loopback; the game's files it packs come back
# to be packaged by tools/android_apk.sh, as `--packager` would package them;
# the exported server runs on the device as its folder has it, and the
# package, installed and launched, plays from the files it carries. Given a
# label pattern, the first button whose accessibility name matches it is
# touched at the middle of its bounds once the client is admitted, as a
# player's finger would (D590): the touch is the UI's press, and the press
# the game's command, which the server takes. The client is stopped by the
# back key, the server as a supervisor stops it, both for their totals.
#
#   tools/android_play_exported.sh <android build tree> [<game directory> [<label pattern>]]
#
# ADB is the adb command with its device (`adb` when unset), for example
# ADB="adb -P 5139 -s emulator-5590"; ANDROID_HOME names the SDK the
# packager uses. Passes when the client made its surface, was admitted, and
# never stalled, and, given a pattern, pressed the button, sent the command,
# and the server took it.
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$(cd "$1" && pwd)"
game="${2:-games/tycoon}"
pattern="${3:-}"
adb=(${ADB:-adb})
name="$(basename "$game")"
package="local.$name"
device=/data/local/tmp/rawframe-play
port=27031
work="$(mktemp -d)"
server_pid=""
trap '[ -z "$server_pid" ] || "${adb[@]}" shell "kill $server_pid" >/dev/null 2>&1 || true
      "${adb[@]}" shell am force-stop "$package" >/dev/null 2>&1 || true
      rm -rf "$work"' EXIT
client_log() { "${adb[@]}" shell run-as "$package" cat files/client.log 2>/dev/null | tr -d '\r'; }

tar -cf "$work/game.tar" "$game"
"${adb[@]}" shell "rm -rf $device && mkdir -p $device/programs"
"${adb[@]}" push -q "$work/game.tar" "$device/game.tar"
"${adb[@]}" push -q "$tree/hosts/export/rawframe-export" "$tree/hosts/cook/rawframe-cook" \
    "$tree/hosts/build/rawframe-build" "$tree/hosts/dedicated_server/rawframe-server" "$device/programs/"
"${adb[@]}" shell "cd $device && tar -xf game.tar && chmod 755 programs/* && programs/rawframe-export $game exported \
    --target android --address 127.0.0.1 --port $port --cook programs/rawframe-cook --build programs/rawframe-build \
    --server programs/rawframe-server >export.log 2>&1" ||
    { echo "the export failed"; "${adb[@]}" shell cat "$device/export.log"; exit 1; }
"${adb[@]}" shell tail -1 "$device/export.log"

# An emulator running arm64 programs by translation, on a loaded machine,
# falls behind for long stretches: neither side stops for it (D576). Each is
# bounded, ten minutes, the server started once the package is installed,
# which a loaded emulator can take minutes over.
relaxed='world.overload_ms = 0\nworld.degraded_ms = 600000\nhost.maximum_iterations = 72000\n'
"${adb[@]}" pull -q "$device/exported/android/game" "$work/game"
printf %b "$relaxed" >>"$work/game/client.conf"
tools/android_apk.sh "$work/$name.apk" "$tree/hosts/android_client/librawframe_client.so" "$package" "$name" \
    "$work/game" >"$work/package.log" 2>&1 || { echo "the packaging failed"; cat "$work/package.log"; exit 1; }

"${adb[@]}" uninstall "$package" >/dev/null 2>&1 || true
"${adb[@]}" install "$work/$name.apk" >/dev/null

printf %b "$relaxed" | "${adb[@]}" shell "cat >>$device/exported/server/server.conf"
# Started in a subshell of its own, which adb does not wait for, as it
# waits for a job of the shell it ran.
"${adb[@]}" shell "cd $device/exported/server && (nohup ./rawframe-server --config server.conf \
    >$device/server.log 2>&1 </dev/null & echo \$! >$device/server.pid)"
server_pid="$("${adb[@]}" shell cat "$device/server.pid" | tr -d '\r')"
for _ in $(seq 100); do
    "${adb[@]}" shell "grep -q '\"code\":\"listening\"' $device/server.log" 2>/dev/null && break
    sleep 0.3
done
"${adb[@]}" shell am start -n "$package/rawframe.client.Activity" >/dev/null
for _ in $(seq 90); do
    client_log | grep -q '"code":"\(bots_admitted\|stopped\)"' && break
    sleep 2
done

if [ -n "$pattern" ]; then
    # The first dump only asks the client's view for its provider: asked
    # again until the button is in it, up to five minutes, as an emulator
    # in CI draws a frame in a second or two.
    bounds=""
    for _ in $(seq 60); do
        "${adb[@]}" shell uiautomator dump /sdcard/rawframe-play.xml >/dev/null 2>&1 || true
        bounds="$("${adb[@]}" shell cat /sdcard/rawframe-play.xml 2>/dev/null | sed 's/<node /\n<node /g' |
            grep "package=\"$package\"" | grep 'class="android.widget.Button"' |
            sed -n 's/.*content-desc="\([^"]*\)".*bounds="\[\([0-9]*\),\([0-9]*\)\]\[\([0-9]*\),\([0-9]*\)\]".*/\2 \3 \4 \5 \1/p' |
            grep -E -- " .*$pattern" | head -1 || true)"
        [ -n "$bounds" ] && break
        sleep 5
    done
    "${adb[@]}" shell rm -f /sdcard/rawframe-play.xml
    if [ -z "$bounds" ]; then
        echo "no button matching $pattern"
    else
        read -r left top right bottom label <<<"$bounds"
        "${adb[@]}" shell input tap $(((left + right) / 2)) $(((top + bottom) / 2))
        echo "touched $label at $(((left + right) / 2)) $(((top + bottom) / 2))"
        # The client has heard the touch once it says so (D559), up to a
        # minute; then its command is on its way, a few frames more.
        for _ in $(seq 60); do
            client_log | grep -q '"code":"input_seen".*"device":"touch"' && break
            sleep 1
        done
        sleep 10
    fi
fi

"${adb[@]}" shell input keyevent BACK
for _ in $(seq 60); do
    client_log | grep -q '"code":"stopped"' && break
    sleep 1
done
"${adb[@]}" shell "kill $server_pid" >/dev/null 2>&1 || true
for _ in $(seq 100); do
    "${adb[@]}" shell "grep -q '\"code\":\"stopped\"' $device/server.log" 2>/dev/null && break
    sleep 0.3
done
server_pid=""

client_log >"$work/client.log"
"${adb[@]}" shell cat "$device/server.log" | tr -d '\r' >"$work/server.log"
grep -o '"code":"server_summary"[^}]*}[^}]*}' "$work/server.log" || true
grep -o '"code":"\(bots_admitted\|bots_summary\|surface_made\|accessibility_ready\|input_summary\|ui_summary\|start_failed\|stopped\)"[^}]*}[^}]*}' \
    "$work/client.log" || { echo "nothing of note:"; tail -20 "$work/client.log"; }
if grep -q '"code":"surface_made"' "$work/client.log" && grep -q '"code":"bots_admitted"' "$work/client.log" &&
    grep -q '"admitted":1,.*"stalled":0,' "$work/client.log" &&
    { [ -z "$pattern" ] || { grep -q '"presses":[1-9]' "$work/client.log" &&
        grep -q '"commandsSent":[1-9]' "$work/client.log" && grep -q '"commandsTaken":[1-9]' "$work/server.log"; }; }; then
    exit 0
fi
echo "== the client's records"
grep -v '"kind":"metric"' "$work/client.log" | cut -c1-600 | head -80
echo "== the server's"
grep -v '"kind":"metric"' "$work/server.log" | cut -c1-400 | tail -20
exit 1
