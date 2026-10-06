#!/usr/bin/env python3
# Clicks in a program's window as a user would (D421): once the program's
# log holds the record that says it is ready (a client's `bots_admitted`,
# Studio's `studio_shown`, D435) and the screen has been lit for two seconds (a server started with a black root window, Xvfb
# -br), the mouse moves to each point given, in the window's pixels from its
# top left (which is the screen's: no window manager runs), and its left
# button is pressed there and let go, a second apart. XTest injects the
# events through the X server, so they reach the window as a real mouse's
# do. Before each press it prints how bright a patch of five by five
# pixels at the point was before the mouse came and after, and whether it
# grew lighter by 8 or more of 255 (D422). A point followed by `:` and
# lower-case text has the text typed after its click, a key at a time,
# then Return (D426). An argument `keys=` and X key names apart by commas
# presses those keys alone, a third of a second apart, and waits a second
# and a half after them (D430). Five seconds after, it stops the program
# whose pid its pid file holds (D430), whose iterations only bound it, and
# waits for the process it watches to end.
#
# usage: click.py <watched pid> <log> <ready code> <pid file>
#                 <x>,<y>[:<text>] | keys=<key>[,<key>...] [...]

import ctypes
import ctypes.util
import os
import signal
import sys
import time


class XImage(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("xoffset", ctypes.c_int),
                ("format", ctypes.c_int), ("data", ctypes.POINTER(ctypes.c_ubyte)), ("byte_order", ctypes.c_int),
                ("bitmap_unit", ctypes.c_int), ("bitmap_bit_order", ctypes.c_int), ("bitmap_pad", ctypes.c_int),
                ("depth", ctypes.c_int), ("bytes_per_line", ctypes.c_int), ("bits_per_pixel", ctypes.c_int)]


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def ready(pid, log, code):
    """Waits until the log holds the record `code`, while the watched
    process runs; whether it did."""
    while alive(pid):
        try:
            with open(log, encoding="utf-8", errors="replace") as records:
                if f'"code":"{code}"' in records.read():
                    return True
        except OSError:
            pass
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
        place, _, text = argument.partition(":")
        points.append((*(int(side) for side in place.split(",")), text))
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
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
        xtest.XTestFakeKeyEvent(display, code, 1, 0)
        x.XFlush(display)
        time.sleep(held)
        xtest.XTestFakeKeyEvent(display, code, 0, 0)
        x.XFlush(display)

    for at, y, text in points:
        if not alive(pid):
            break
        if at is None:
            for name in text:
                press(name, 0.1)
                time.sleep(0.33)
            print(f"pressed {','.join(text)}")
            time.sleep(1.5)
            continue
        # Moved there first, so the press is where the pointer already is.
        before = brightness(x, display, root, at, y)
        xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
        x.XFlush(display)
        # A loaded machine draws late: up to three seconds for it to show.
        after = before
        for _ in range(12):
            time.sleep(0.25)
            after = brightness(x, display, root, at, y)
            if after >= before + 8:
                break
        print(f"at {at},{y} the screen was {before:.0f} bright before the mouse and {after:.0f} under it: "
              f"{'lighter' if after >= before + 8 else 'not lighter'}")
        xtest.XTestFakeButtonEvent(display, 1, 1, 0)
        x.XFlush(display)
        time.sleep(0.2)
        xtest.XTestFakeButtonEvent(display, 1, 0, 0)
        x.XFlush(display)
        print(f"clicked at {at},{y}")
        time.sleep(1)
        if text:
            for name in [*text, "Return"]:
                press(name, 0.05)
                time.sleep(0.1)
            print(f"typed {text} at {at},{y}")
            time.sleep(1)
    time.sleep(5)
    stop(pid_file)
    while alive(pid):
        time.sleep(0.25)


if __name__ == "__main__":
    main()
