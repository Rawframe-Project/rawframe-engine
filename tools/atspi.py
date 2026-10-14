#!/usr/bin/env python3
# A program's controls found by name through its accessibility tree (D774),
# so a test presses "Undo" rather than a point that moves whenever the
# layout does. The tree is read over the AT-SPI bus a test starts for the
# program (AT_SPI_BUS_ADDRESS, as read_aloud.sh does), with busctl, so
# nothing beyond the system's tools is needed. A control is named as the
# program names it for assistive technology: a button by its words, a
# column by its heading. Its place is its extents in the window, which
# with no window manager running is the screen.
#
# usage: atspi.py <bus address> <application name> <name>[#<n>] [<role>]
#        prints the centre of the n-th control so named (from 1), "x,y"

import re
import subprocess
import sys


def call(address, *arguments):
    done = subprocess.run(["busctl", "--address=" + address, *arguments], capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else ""


def application(address, name):
    """The bus name of the application called `name`, or None."""
    for line in call(address, "list", "--no-legend").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return parts[0]
    return None


def walk(address, owner, path="/org/a11y/atspi/accessible/root", depth=0):
    """Every node under `path`, depth first, as (path, role, name)."""
    role = re.search(r'^s "(.*)"$', call(address, "call", owner, path, "org.a11y.atspi.Accessible", "GetRoleName"),
                     re.M)
    label = re.search(r'^s "(.*)"$',
                      call(address, "get-property", owner, path, "org.a11y.atspi.Accessible", "Name"), re.M)
    yield path, role.group(1) if role else "", label.group(1) if label else ""
    if depth >= 12:
        return
    children = call(address, "call", owner, path, "org.a11y.atspi.Accessible", "GetChildren")
    for child in re.findall(r'"(/[^"]*)"', children):
        yield from walk(address, owner, child, depth + 1)


def extents(address, owner, path):
    """A node's (x, y, width, height) in its window's pixels, or None."""
    # Coordinate type 1 is the window's.
    said = call(address, "call", owner, path, "org.a11y.atspi.Component", "GetExtents", "u", "1")
    numbers = re.findall(r"-?\d+", said)
    return tuple(int(n) for n in numbers[-4:]) if len(numbers) >= 4 else None


def centre(address, owner, wanted, role=None):
    """The centre of the control named `wanted`, its n-th when it ends with
    `#n`, of `role` if given; None when there is none."""
    count = 1
    if "#" in wanted:
        wanted, nth = wanted.rsplit("#", 1)
        count = int(nth)
    for path, node_role, name in walk(address, owner):
        if name != wanted or (role and node_role != role):
            continue
        count -= 1
        if count == 0:
            box = extents(address, owner, path)
            return None if box is None else (box[0] + box[2] // 2, box[1] + box[3] // 2)
    return None


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit("usage: atspi.py <bus address> <application name> <name>[#<n>] [<role>]")
    owner = application(sys.argv[1], sys.argv[2])
    point = centre(sys.argv[1], owner, sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None) if owner else None
    if point is None:
        sys.exit("no control named " + sys.argv[3])
    print("%d,%d" % point)


if __name__ == "__main__":
    main()
