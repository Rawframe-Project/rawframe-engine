#!/usr/bin/env bash
# Runs the arena on runners_boxed.conf (D369) and looks at the picture it
# captured, 640 by 480: the 16:9 view is the middle 360 rows, the 60 above
# and below it bars of runners' slate (24 28 40, within a step of rounding),
# and the view is not: it is cleared black and drawn over.
#
# usage: runners_boxed.sh <rawframe-arena> <configuration> <capture>
set -uo pipefail

rm -f "$3"
output=$("$(dirname "$0")/runners_drawn.sh" "$1" "$2")
status=$?
printf '%s\n' "$output"
if [ "$status" -ne 0 ] || grep -q '^skip: no device' <<<"$output"; then
    exit "$status"
fi
python3 - "$3" <<'PYTHON'
import sys

image = open(sys.argv[1], "rb").read()
width = image[12] | (image[13] << 8)
height = image[14] | (image[15] << 8)
assert (width, height) == (640, 480), (width, height)
pixels = image[18:]


SLATE = (24, 28, 40)


def pixel(row, column):
    at = (row * width + column) * 4
    return (pixels[at + 2], pixels[at + 1], pixels[at])


def slate(row):
    return all(all(abs(a - b) <= 1 for a, b in zip(pixel(row, column), SLATE)) for column in range(width))


bars = sum(1 for row in list(range(0, 60)) + list(range(420, 480)) if slate(row))
view = sum(1 for row in range(60, 420) if slate(row))
print(f"boxed: {bars} of 120 bar rows slate, {view} view rows slate")
sys.exit(0 if bars == 120 and view == 0 else 1)
PYTHON
