#!/usr/bin/env python3
# A program's controls found by name through its accessibility tree (D774),
# so a test presses "Undo" rather than a point that moves whenever the
# layout does. The tree is read over the AT-SPI bus a test starts for the
# program (AT_SPI_BUS_ADDRESS, as read_aloud.sh does), with busctl, so
# nothing beyond the system's tools is needed. A control is named as the
# program names it for assistive technology: a button by its words, a
# column by its heading. Its place is its extents in the window, which
# with no window manager running is the screen. Where Python's dbus module
# is installed the tree is read in this process, a call per question
# instead of a program per question, which a test clicking many names
# needs; busctl otherwise.
#
# usage: atspi.py <bus address> <application name> <name>[#<n>] [<role>]
#        prints the centre of the n-th control so named (from 1), "x,y"

import re
import subprocess
import sys
import time


try:
    import dbus
except ImportError:
    dbus = None

_connections = {}


def _bus(address):
    if address not in _connections:
        _connections[address] = dbus.bus.BusConnection(address)
    return _connections[address]


def call(address, *arguments):
    done = subprocess.run(["busctl", "--address=" + address, *arguments], capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else ""


def application(address, name):
    """The bus name of the application called `name`, or None."""
    if dbus is not None:
        # By the process's name, as busctl lists it: asking each name on
        # the bus would wait on ones that never answer.
        bus = _bus(address)
        daemon = bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus", introspect=False)
        for owner in bus.list_names():
            if not owner.startswith(":"):
                continue
            try:
                pid = int(daemon.GetConnectionUnixProcessID(owner, dbus_interface="org.freedesktop.DBus"))
                with open("/proc/%d/comm" % pid, encoding="utf-8") as comm:
                    if comm.read().strip() == name[:15]:
                        return str(owner)
            except (dbus.exceptions.DBusException, OSError, ValueError):
                continue
        return None
    for line in call(address, "list", "--no-legend").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return parts[0]
    return None


def walk(address, owner, path="/org/a11y/atspi/accessible/root", depth=0):
    """Every node under `path`, depth first, as (path, role, name)."""
    if dbus is not None:
        node = _bus(address).get_object(owner, path, introspect=False)
        try:
            role = str(node.GetRoleName(dbus_interface="org.a11y.atspi.Accessible"))
            label = str(node.Get("org.a11y.atspi.Accessible", "Name", dbus_interface="org.freedesktop.DBus.Properties"))
            children = node.GetChildren(dbus_interface="org.a11y.atspi.Accessible")
        except dbus.exceptions.DBusException:
            return
        yield path, role, label
        if depth < 12:
            for _, child in children:
                yield from walk(address, owner, str(child), depth + 1)
        return
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
    if dbus is not None:
        try:
            box = _bus(address).get_object(owner, path, introspect=False).GetExtents(
                dbus.UInt32(1), dbus_interface="org.a11y.atspi.Component")
        except dbus.exceptions.DBusException:
            return None
        return tuple(int(side) for side in box)
    # Coordinate type 1 is the window's.
    said = call(address, "call", owner, path, "org.a11y.atspi.Component", "GetExtents", "u", "1")
    numbers = re.findall(r"-?\d+", said)
    return tuple(int(n) for n in numbers[-4:]) if len(numbers) >= 4 else None


def scroll_to(address, owner, path):
    """Asks the program to scroll the node at `path` into view, as a screen
    reader does before acting on it."""
    if dbus is not None:
        try:
            _bus(address).get_object(owner, path, introspect=False).ScrollTo(
                dbus.UInt32(0), dbus_interface="org.a11y.atspi.Component")
        except dbus.exceptions.DBusException:
            pass
        return
    call(address, "call", owner, path, "org.a11y.atspi.Component", "ScrollTo", "u", "0")


def settled(address, owner, path):
    """The node's extents once a scroll it asked for has moved it: the same
    twice running, or after a second; first a moment for the scroll to be
    applied at all, a program idle drawing only a few frames a second
    (D925)."""
    time.sleep(0.6)
    box = extents(address, owner, path)
    for _ in range(10):
        time.sleep(0.1)
        again = extents(address, owner, path)
        if again == box:
            return box
        box = again
    return box


def centre(address, owner, wanted, role=None, unless=None):
    """The centre of the control named `wanted`, its n-th when it ends with
    `#n`, of `role` if given and not of `unless` (D925), scrolled into view
    first (D776); None when there is none."""
    count = 1
    if "#" in wanted:
        wanted, nth = wanted.rsplit("#", 1)
        count = int(nth)
    for path, node_role, name in walk(address, owner):
        if name != wanted or (role and node_role != role) or (unless and node_role == unless):
            continue
        count -= 1
        if count == 0:
            scroll_to(address, owner, path)
            box = settled(address, owner, path)
            return None if box is None else (box[0] + box[2] // 2, box[1] + box[3] // 2)
    return None


def at(address, owner, x, y):
    """The named control smallest around the point (x, y), as `name` or
    `name#n` when others before it bear its name; None where none is."""
    best = None
    seen = {}
    for path, _, name in walk(address, owner):
        if not name:
            continue
        seen[name] = seen.get(name, 0) + 1
        box = extents(address, owner, path)
        if box is None or not (box[0] <= x < box[0] + box[2] and box[1] <= y < box[1] + box[3]):
            continue
        if best is None or box[2] * box[3] <= best[0]:
            best = (box[2] * box[3], name if seen[name] == 1 else "%s#%d" % (name, seen[name]))
    return best[1] if best else None


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
