#!/usr/bin/env bash
# A running Android client's UI as accessibility services read it (D576):
# uiautomator dumps the screen's accessibility tree, which asks the client's
# view for its node provider; the first asking makes the client's access, so
# the dump is asked again until the UI's nodes are in it, up to a minute (a
# loaded emulator draws a frame a second). Prints one line a node of the
# package: "a11y: <class> text=<text> name=<content description>".
#
#   ADB="adb -s emulator-5590" tools/android_read_aloud.sh <package>
set -uo pipefail
adb=(${ADB:-adb})
package="$1"

for _ in $(seq 12); do
    "${adb[@]}" shell uiautomator dump /sdcard/rawframe-a11y.xml >/dev/null 2>&1
    dump="$("${adb[@]}" shell cat /sdcard/rawframe-a11y.xml 2>/dev/null)"
    # The window's view and the provider's root are views; the UI's nodes
    # are what lies under them.
    if sed 's/<node /\n<node /g' <<<"$dump" | grep "package=\"$package\"" | grep -qv 'class="android.view.View"\|class="android.widget.FrameLayout"\|class="android.widget.LinearLayout"'; then
        sed 's/<node /\n<node /g' <<<"$dump" | grep "package=\"$package\"" |
            sed -n 's/.*text="\([^"]*\)".*class="\([^"]*\)".*content-desc="\([^"]*\)".*/a11y: \2 text=\1 name=\3/p'
        "${adb[@]}" shell rm -f /sdcard/rawframe-a11y.xml
        exit 0
    fi
    sleep 5
done
echo "a11y: no UI nodes of $package"
exit 1
