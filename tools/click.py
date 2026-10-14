#!/usr/bin/env python3
# Clicks in a program's window as a user would (D421): once the program's
# log holds the record that says it is ready (a client's `bots_admitted`,
# Studio's `studio_shown`, D435) and the screen has been lit for two seconds
# (a server started with a black root window, Xvfb -br), the mouse moves to
# each point given, in the window's pixels from its top left (which is the
# screen's: no window manager runs), and its left button is pressed there
# and let go, a second apart. XTest injects the events through the X server,
# so they reach the window as a real mouse's do. Before each press it prints
# how bright a patch of five by five pixels at the point was before the
# mouse came and after, and whether it grew lighter by 8 or more of 255
# (D422), waiting up to three seconds for that; with CLICK_CURSOR set, for a
# program drawing its own cursor, it waits for the point to hold still
# before and up to fifteen seconds after (D453b). A point followed by `:`
# and lower-case text has the text typed after its click, a key at a time,
# then Return (D426); a dot, a comma, a minus, and a space are typed as
# their keys, a colon with Shift (D473), and a slash, an underscore, and
# capitals as their keys too, so a path is typed whole (D505). An argument `keys=` and X key names apart by commas presses
# those keys alone, a name joined by pluses as a chord, the keys before the
# last held through it (D777), a third of a second apart, and waits a second and a half
# after them (D430). An argument `wheel=`, a point, and a count turns the
# wheel there that many detents toward the user, or away for a count below
# nought, 0.15 seconds apart (D441), and `drag=` and two points presses the
# left button at the first, carries it to the second, and lets it go there
# (D457), holding the X key a fifth part names through it, Shift_L or
# Control_L (D463). An argument `frozen=` and X key names presses them with
# the program stopped, so it reads them in one frame (D458), and
# `frozenclick=` and a point with its text clicks there and types it with
# the program stopped, once the mouse is over the point (D477). The
# arguments `freeze` and `thaw` stop the program and let it go on, so what
# comes between, clicks and the wheel among it, is read in one frame
# (D479). An argument `quick=` and X key names presses them a few
# milliseconds apart with the program running, as a quick player does,
# so they reach it within one world tick (D506). An argument
# `move=` and a point moves the pointer there with no press, a second
# before going on (D468), and `orbit=` and two points drags as `drag=`
# does with the right button (D469), a key a fifth part names held through
# it too (D472). An argument
# `wait=` and a number of seconds waits that long, and `until=` and a
# record's code waits, up to three minutes, until the log holds it (D445),
# one whose field has a value where `,name=value` follows (D476), or a
# number at least that where `,name>=value` does; ending `#` and a count,
# that many such records, and `@` and a path, in that log beside the
# watched one, such as a played client's (D734); ending `~` and a number
# of seconds, up to that long instead, for what a loaded sanitized tree
# may take longer than three minutes to make, such as a cook. A record
# never seen in that time is said ("never saw"), which the tests read as a
# failure: the wait it stands for was three minutes lost (D627). An argument `edit=`
# and a file, the text in it to replace, and what with, separated by
# colons, edits the file as an author would while the game plays (D634).
# A point may be a control's name instead, `@` and the name as the program
# gives it to assistive technology, `#n` after it for the n-th so named
# (D774): found through the accessibility bus the test started
# (AT_SPI_BUS_ADDRESS) by tools/atspi.py when its turn comes, since what
# came before may have moved it, in the application CLICK_APPLICATION
# names, rawframe-studio unless set; between `freeze` and `thaw`, before
# the program is stopped, since a stopped program answers nobody.
# Five seconds after, it stops the program whose pid its pid file holds
# (D430), whose iterations only bound it, and waits for the process it
# watches to end; half a second, where the last argument waited for a
# record that came, since what the test asked for has been seen (D493).
#
# usage: click.py <watched pid> <log> <ready code> <pid file>
#                 <x>,<y>[:<text>] | @<name>[#<n>][:<text>] | keys=<key>[,<key>...]
#                 | wheel=<x>,<y>,<turns>[,<key>] | drag=<x>,<y>,<x>,<y>[,<key>]
#                 | frozen=<key>[,<key>...] | quick=<key>[,<key>...]
#                 | frozenclick=<x>,<y>:<text>
#                 | move=<x>,<y> | freeze | thaw
#                 | orbit=<x>,<y>,<x>,<y>[,<key>] | wait=<seconds>
#                 | until=<code>[,<field>=<value> | ,<field>>=<number>][#<count>][@<log>][~<seconds>]
#                 | edit=<file>:<text>:<text>
#                 [...]

import ctypes
import ctypes.util
import os
import re
import signal
import sys
import time

# tools/atspi.py is imported from here; no cache of it is left in tools/.
sys.dont_write_bytecode = True


class XImage(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("xoffset", ctypes.c_int),
                ("format", ctypes.c_int), ("data", ctypes.POINTER(ctypes.c_ubyte)), ("byte_order", ctypes.c_int),
                ("bitmap_unit", ctypes.c_int), ("bitmap_bit_order", ctypes.c_int), ("bitmap_pad", ctypes.c_int),
                ("depth", ctypes.c_int), ("bytes_per_line", ctypes.c_int), ("bits_per_pixel", ctypes.c_int)]


# Characters typed whose X key names are words.
# A vector's brackets among them, as a material's override is typed (D722),
# and an equals sign (D731).
KEYS = {".": "period", ",": "comma", "-": "minus", " ": "space", "/": "slash", "[": "bracketleft",
        "]": "bracketright", "=": "equal"}
# Characters typed with Shift held, by the key that types them (D473), a
# color's # among them (D637).
SHIFTED = {":": "semicolon", "_": "underscore", "#": "3"}
# Capitals typed with Shift held, as a path may hold them (D505).
SHIFTED.update({letter: letter.lower() for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


class Frozen(str):
    """A click's text typed with the program stopped (D477)."""


def logged(log, code):
    """Whether the log holds the record `code`; a code followed by
    `,name=value`, a record of it whose field `name` is `value` (D476), and
    by `,name>=number`, one whose field is a number at least that (D627);
    ending `#count`, that many records of it, and `@path`, in the log at
    that path beside `log` (D734)."""
    if "@" in code:
        code, _, beside = code.rpartition("@")
        log = os.path.join(os.path.dirname(log), beside)
    count = 1
    if "#" in code:
        code, _, times = code.rpartition("#")
        count = int(times)
    code, _, field = code.partition(",")
    least = None
    if ">=" in field:
        name, _, number = field.partition(">=")
        least = re.compile(f'"{re.escape(name)}":(-?[0-9]+)')
        wanted = ""
    else:
        name, _, value = field.partition("=")
        wanted = f'"{name}":{value}' if field else ""

    def matches(line):
        if f'"code":"{code}"' not in line or wanted not in line:
            return False
        if least is None:
            return True
        found = least.search(line)
        return found is not None and int(found.group(1)) >= int(number)

    try:
        with open(log, encoding="utf-8", errors="replace") as records:
            return sum(1 for line in records if matches(line)) >= count
    except OSError:
        return False


def ready(pid, log, code):
    """Waits until the log holds the record `code`, while the watched
    process runs; whether it did."""
    while alive(pid):
        if logged(log, code):
            return True
        time.sleep(0.25)
    return False


def stop(pid_file):
    """Asks the program to stop, as a user closing it would."""
    try:
        with open(pid_file, encoding="utf-8") as told:
            os.kill(int(told.read().strip()), signal.SIGTERM)
    except (OSError, ValueError):
        pass


def brightness(x, display, root, at, y):
    """The mean of the red, green, and blue of the five by five pixels
    around a point."""
    image = x.XGetImage(display, root, at - 2, y - 2, 5, 5, 0xFFFFFFFF, 2)
    if not image:
        return 0
    info = image.contents
    data = bytes(ctypes.cast(info.data, ctypes.POINTER(ctypes.c_ubyte * (info.bytes_per_line * info.height))).contents)
    values = [data[row * info.bytes_per_line + column * 4 + channel]
              for row in range(5) for column in range(5) for channel in range(3)]
    return sum(values) / len(values)


def named_point(name):
    """The centre of the control called `name` (D774), waiting up to six
    seconds for it to be shown; (None, None) if it never is."""
    import atspi

    address = os.environ.get("AT_SPI_BUS_ADDRESS", "")
    application = os.environ.get("CLICK_APPLICATION", "rawframe-studio")
    for _ in range(30):
        owner = atspi.application(address, application) if address else None
        point = atspi.centre(address, owner, name) if owner else None
        if point is not None:
            return point
        time.sleep(0.2)
    return None, None


def point_name(x, y):
    """The name of the control at (x, y), for CLICK_NAME_POINTS."""
    import atspi

    address = os.environ.get("AT_SPI_BUS_ADDRESS", "")
    owner = atspi.application(address, os.environ.get("CLICK_APPLICATION", "rawframe-studio")) if address else None
    return atspi.at(address, owner, x, y) if owner else None


def main():
    pid = int(sys.argv[1])
    log = sys.argv[2]
    code = sys.argv[3]
    pid_file = sys.argv[4]
    points = []
    for argument in sys.argv[5:]:
        if argument.startswith("keys="):
            points.append((None, None, argument[len("keys="):].split(",")))
            continue
        if argument.startswith("frozen="):
            points.append((None, "frozen", argument[len("frozen="):].split(",")))
            continue
        if argument.startswith("quick="):
            points.append((None, "quick", argument[len("quick="):].split(",")))
            continue
        if argument in ("freeze", "thaw"):
            points.append((None, None, (argument,)))
            continue
        if argument.startswith("until="):
            points.append((None, None, ("until", argument[len("until="):])))
            continue
        if argument.startswith("edit="):
            path, old, new = argument[len("edit="):].rsplit(":", 2)
            points.append((None, None, ("edit", path, old, new)))
            continue
        if argument.startswith("wait="):
            points.append((None, None, float(argument[len("wait="):])))
            continue
        if argument.startswith("move="):
            toX, toY = (int(part) for part in argument[len("move="):].split(","))
            points.append((toX, toY, ("move",)))
            continue
        if argument.startswith("drag="):
            parts = argument[len("drag="):].split(",")
            fromX, fromY, toX, toY = (int(part) for part in parts[:4])
            points.append((fromX, fromY, ("drag", toX, toY, parts[4] if len(parts) > 4 else "", 1)))
            continue
        if argument.startswith("orbit="):
            parts = argument[len("orbit="):].split(",")
            fromX, fromY, toX, toY = (int(part) for part in parts[:4])
            points.append((fromX, fromY, ("drag", toX, toY, parts[4] if len(parts) > 4 else "", 3)))
            continue
        if argument.startswith("wheel="):
            parts = argument[len("wheel="):].split(",")
            at, y, turns = (int(part) for part in parts[:3])
            points.append((at, y, ("wheel", turns, parts[3]) if len(parts) > 3 else turns))
            continue
        frozen = argument.startswith("frozenclick=")
        place, _, text = argument[len("frozenclick=") if frozen else 0:].partition(":")
        if place.startswith("@"):
            points.append((place, None, Frozen(text) if frozen else text))
            continue
        points.append((*(int(side) for side in place.split(",")), Frozen(text) if frozen else text))
    x = ctypes.CDLL(ctypes.util.find_library("X11"))
    xtest = ctypes.CDLL("libXtst.so.6")
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XDefaultRootWindow.restype = ctypes.c_ulong
    x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x.XGetImage.restype = ctypes.POINTER(XImage)
    x.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                            ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
    x.XFlush.argtypes = [ctypes.c_void_p]
    xtest.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    xtest.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    x.XStringToKeysym.restype = ctypes.c_ulong
    x.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x.XKeysymToKeycode.restype = ctypes.c_ubyte
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    display = x.XOpenDisplay(None)
    if not display:
        sys.exit("click.py: no X display")
    root = x.XDefaultRootWindow(display)
    if not ready(pid, log, code):
        print(f"the program ended before it said {code}")
    lit = 0
    while alive(pid) and lit < 8:
        time.sleep(0.25)
        image = x.XGetImage(display, root, 0, 0, 64, 64, 0xFFFFFFFF, 2)
        if image:
            info = image.contents
            data = ctypes.cast(info.data, ctypes.POINTER(ctypes.c_ubyte * (info.bytes_per_line * info.height))).contents
            lit += 1 if any(bytes(data)) else 0
    def press(name, held):
        # A chord, "Control_L+z": the keys before the last held through it
        # (D777).
        *holding, name = name.split("+") if len(name) > 1 else [name]
        codes = [x.XKeysymToKeycode(display, x.XStringToKeysym(each.encode())) for each in holding]
        for code in codes:
            xtest.XTestFakeKeyEvent(display, code, 1, 0)
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
        xtest.XTestFakeKeyEvent(display, code, 1, 0)
        x.XFlush(display)
        time.sleep(held)
        xtest.XTestFakeKeyEvent(display, code, 0, 0)
        for code in reversed(codes):
            xtest.XTestFakeKeyEvent(display, code, 0, 0)
        x.XFlush(display)

    seen = False
    for index, (at, y, text) in enumerate(points):
        seen = False
        if not alive(pid):
            break
        if isinstance(text, tuple) and text[0] == "freeze":
            # A stopped program answers nobody: the names clicked before
            # it goes on again are found while it still runs.
            for later in range(index + 1, len(points)):
                if isinstance(points[later][2], tuple) and points[later][2][0] == "thaw":
                    break
                if isinstance(points[later][0], str):
                    named = points[later][0]
                    found = named_point(named[1:])
                    print(f"found {named[1:]} at {found[0]},{found[1]}" if found[0] is not None
                          else f"no control named {named[1:]}")
                    points[later] = (*found, points[later][2])
        if isinstance(at, str):
            named = at
            at, y = named_point(named[1:])
            if at is None:
                print(f"no control named {named[1:]}")
                continue
            print(f"found {named[1:]} at {at},{y}")
        elif os.environ.get("CLICK_NAME_POINTS") and isinstance(text, str):
            # Which control a point is on, for moving a test to names.
            print(f"point {at},{y} is on {point_name(at, y)}")
        if isinstance(text, tuple) and text[0] == "wheel":
            # The wheel turned with a key held through it (D472).
            xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
            x.XFlush(display)
            time.sleep(0.25)
            held = x.XKeysymToKeycode(display, x.XStringToKeysym(text[2].encode()))
            xtest.XTestFakeKeyEvent(display, held, 1, 0)
            x.XFlush(display)
            time.sleep(0.2)
            for _ in range(abs(text[1])):
                xtest.XTestFakeButtonEvent(display, 5 if text[1] > 0 else 4, 1, 0)
                xtest.XTestFakeButtonEvent(display, 5 if text[1] > 0 else 4, 0, 0)
                x.XFlush(display)
                time.sleep(0.15)
            time.sleep(0.2)
            xtest.XTestFakeKeyEvent(display, held, 0, 0)
            x.XFlush(display)
            print(f"turned the wheel {text[1]} at {at},{y} holding {text[2]}")
            time.sleep(1)
            continue
        if isinstance(text, tuple) and text[0] in ("freeze", "thaw"):
            with open(pid_file, encoding="utf-8") as told:
                os.kill(int(told.read().strip()), signal.SIGSTOP if text[0] == "freeze" else signal.SIGCONT)
            print("froze the program" if text[0] == "freeze" else "let the program go on")
            time.sleep(0.5 if text[0] == "freeze" else 1.5)
            continue
        if isinstance(text, tuple) and text[0] == "edit":
            with open(text[1], encoding="utf-8") as read:
                before = read.read()
            with open(text[1], "w", encoding="utf-8") as written:
                written.write(before.replace(text[2], text[3]))
            print(f"edited {text[1]}: {text[2]} to {text[3]}{'' if text[2] in before else ', which it did not hold'}")
            continue
        if isinstance(text, tuple) and text[0] == "move":
            xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
            x.XFlush(display)
            print(f"moved to {at},{y}")
            time.sleep(1)
            continue
        if isinstance(text, tuple) and text[0] == "drag":
            # Pressed at the point, carried to the other in ten steps a
            # twentieth of a second apart, and let go there (D457).
            xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
            x.XFlush(display)
            time.sleep(0.5)
            # A key held through the drag, Shift_L or Control_L (D463).
            held = x.XKeysymToKeycode(display, x.XStringToKeysym(text[3].encode())) if text[3] else 0
            if held:
                xtest.XTestFakeKeyEvent(display, held, 1, 0)
                x.XFlush(display)
                time.sleep(0.2)
            xtest.XTestFakeButtonEvent(display, text[4], 1, 0)
            x.XFlush(display)
            for step in range(1, 11):
                time.sleep(0.05)
                xtest.XTestFakeMotionEvent(display, -1, at + (text[1] - at) * step // 10,
                                           y + (text[2] - y) * step // 10, 0)
                x.XFlush(display)
            time.sleep(0.2)
            xtest.XTestFakeButtonEvent(display, text[4], 0, 0)
            x.XFlush(display)
            if held:
                time.sleep(0.2)
                xtest.XTestFakeKeyEvent(display, held, 0, 0)
                x.XFlush(display)
            print(f"{'orbited' if text[4] == 3 else 'dragged'} from {at},{y} to {text[1]},{text[2]}"
                  f"{' holding ' + text[3] if text[3] else ''}")
            time.sleep(1)
            continue
        if isinstance(text, tuple):
            # Up to three minutes for the record, however loaded the machine,
            # or as long as it says.
            code, bound = text[1], 180.0
            if "~" in code:
                code, _, seconds = code.rpartition("~")
                bound = float(seconds)
            found = False
            for _ in range(int(bound * 4)):
                found = logged(log, code)
                if found or not alive(pid):
                    break
                time.sleep(0.25)
            print(f"{'saw' if found else 'never saw'} {code}")
            seen = found
            continue
        if isinstance(text, float):
            time.sleep(text)
            print(f"waited {text:g} seconds")
            continue
        if isinstance(text, int):
            # Button 5 turns the wheel toward the user, 4 away from them.
            xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
            x.XFlush(display)
            time.sleep(0.25)
            for _ in range(abs(text)):
                xtest.XTestFakeButtonEvent(display, 5 if text > 0 else 4, 1, 0)
                xtest.XTestFakeButtonEvent(display, 5 if text > 0 else 4, 0, 0)
                x.XFlush(display)
                time.sleep(0.15)
            print(f"turned the wheel {text} at {at},{y}")
            # What scrolls eases into place over frames, slower on a loaded
            # machine: wait until the column about the wheel holds still,
            # four looks in a row, before the next step (D475).
            def column():
                return [brightness(x, display, root, at, y + offset) for offset in (-150, -75, 0, 75, 150)]
            before = column()
            steady = 0
            for _ in range(60):
                if steady >= 4:
                    break
                time.sleep(0.25)
                now = column()
                steady = steady + 1 if all(abs(a - b) < 1 for a, b in zip(now, before)) else 0
                before = now
            time.sleep(0.25)
            continue
        if at is None:
            # Frozen keys are pressed while the program is stopped, so it
            # reads them all in one frame, as a slow machine's does (D458).
            if y == "frozen":
                with open(pid_file, encoding="utf-8") as told:
                    frozen = int(told.read().strip())
                os.kill(frozen, signal.SIGSTOP)
            for name in text:
                press(name, 0.001 if y == "quick" else 0.1)
                time.sleep(0.002 if y == "quick" else 0.33)
            if y == "frozen":
                time.sleep(0.5)
                os.kill(frozen, signal.SIGCONT)
            print(f"pressed {y + ' ' if y else ''}{','.join(text)}")
            time.sleep(1.5)
            continue
        # Moved there first, so the press is where the pointer already is.
        # A program that draws its own cursor is checked for it (CLICK_CURSOR
        # set): the baseline is taken once the point has held still a second,
        # up to ten, since a loaded machine may still be drawing its first
        # frames there, and the cursor is waited for up to fifteen seconds (a
        # sanitized client at load 130 drew 25 frames in a run). Otherwise a
        # click waits half a second: no other test asks for the light, and
        # input read in one frame is taken in its order (D486, D489), so the
        # three seconds it waited cost a full check's every tree six minutes
        # of clicks lighting nothing (D493).
        cursor = bool(os.environ.get("CLICK_CURSOR"))
        before = brightness(x, display, root, at, y)
        steady = 0
        for _ in range(40 if cursor else 0):
            if steady >= 4:
                break
            time.sleep(0.25)
            now = brightness(x, display, root, at, y)
            steady = steady + 1 if abs(now - before) < 2 else 0
            before = now
        xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
        x.XFlush(display)
        after = before
        for _ in range(60 if cursor else 2):
            time.sleep(0.25)
            after = brightness(x, display, root, at, y)
            if after >= before + 8:
                break
        print(f"at {at},{y} the screen was {before:.0f} bright before the mouse and {after:.0f} under it: "
              f"{'lighter' if after >= before + 8 else 'not lighter'}")
        stopped = None
        if isinstance(text, Frozen):
            with open(pid_file, encoding="utf-8") as told:
                stopped = int(told.read().strip())
            os.kill(stopped, signal.SIGSTOP)
        xtest.XTestFakeButtonEvent(display, 1, 1, 0)
        x.XFlush(display)
        time.sleep(0.2)
        xtest.XTestFakeButtonEvent(display, 1, 0, 0)
        x.XFlush(display)
        print(f"{'clicked frozen' if stopped else 'clicked'} at {at},{y}")
        time.sleep(1)
        if text:
            shift = x.XKeysymToKeycode(display, x.XStringToKeysym(b"Shift_L"))
            for letter in [*text, "Return"]:
                if letter in SHIFTED:
                    xtest.XTestFakeKeyEvent(display, shift, 1, 0)
                    x.XFlush(display)
                    press(SHIFTED[letter], 0.05)
                    xtest.XTestFakeKeyEvent(display, shift, 0, 0)
                    x.XFlush(display)
                else:
                    press(letter if letter == "Return" else KEYS.get(letter, letter), 0.05)
                time.sleep(0.1)
            print(f"typed {text} at {at},{y}")
            time.sleep(1)
        if stopped is not None:
            time.sleep(0.5)
            os.kill(stopped, signal.SIGCONT)
            time.sleep(1.5)
    time.sleep(0.5 if seen else 5)
    stop(pid_file)
    while alive(pid):
        time.sleep(0.25)


if __name__ == "__main__":
    main()
