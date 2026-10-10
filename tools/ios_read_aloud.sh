#!/usr/bin/env bash
# A running iOS client's UI as accessibility clients read it, in a booted
# simulator (D588): idb reads the screen's accessibility elements, which
# asks the client's view for its elements; the first asking makes the
# client's access, so the reading is asked again until a button the pattern
# names is in it, up to five minutes, as the client may still be on its way
# (an export made in the simulator first takes a minute or two). Prints one line an element,
# "a11y: <type> <label>"; then touches the first button whose label matches
# at the middle of its frame, as a player's finger would: "a11y: touched
# <label>".
#
#   tools/ios_read_aloud.sh <device> <label pattern>
#
# idb is Facebook's (`pip install fb-idb`, with `idb_companion` from
# Homebrew's facebook/fb tap).
set -uo pipefail
device="$1"
pattern="$2"
elements="$(mktemp)"
trap 'rm -f "$elements"' EXIT

# Each reading unlike the one before it, said in a line on standard error:
# what was read while the client came, for when it is never found.
said=""
for _ in $(seq 150); do
    idb ui describe-all --udid "$device" --json >"$elements" 2>/dev/null || true
    seen="$(python3 - "$elements" <<'PY' 2>/dev/null
import json, sys
elements = json.load(open(sys.argv[1]))
print(f"read {len(elements)} elements of pid {elements[0].get('pid') if elements else None}:",
      ", ".join(f"{e.get('type')} {e.get('AXLabel') or ''}".strip() for e in elements[:6]))
PY
)"
    [ "$seen" = "$said" ] || echo "a11y: $seen" >&2
    said="$seen"
    if python3 - "$elements" "$pattern" <<'PY' 2>/dev/null
import json, re, sys
found = [e for e in json.load(open(sys.argv[1]))
         if e.get("type") == "Button" and re.search(sys.argv[2], e.get("AXLabel") or "")]
sys.exit(0 if found else 1)
PY
    then
        python3 - "$elements" "$pattern" <<'PY'
import json, re, sys
elements = json.load(open(sys.argv[1]))
for e in elements:
    print(f"a11y: {e.get('type')} {e.get('AXLabel') or ''}")
button = next(e for e in elements
              if e.get("type") == "Button" and re.search(sys.argv[2], e.get("AXLabel") or ""))
frame = button["frame"]
print(f"tap {frame['x'] + frame['width'] / 2:.0f} {frame['y'] + frame['height'] / 2:.0f} {button['AXLabel']}")
PY
        break
    fi
    sleep 2
done >"$elements.read"
cat "$elements.read"
tap="$(grep '^tap ' "$elements.read")"
rm -f "$elements.read"
if [ -z "$tap" ]; then
    echo "a11y: no button matching $pattern; the last reading:"
    python3 - "$elements" <<'PY' 2>/dev/null || cat "$elements"
import json, sys
for e in json.load(open(sys.argv[1])):
    print(f"a11y: {e.get('type')} {e.get('AXLabel') or ''} (pid {e.get('pid')})")
PY
    exit 1
fi
read -r _ x y label <<<"$tap"
idb ui tap "$x" "$y" --udid "$device" || exit 1
echo "a11y: touched $label"
