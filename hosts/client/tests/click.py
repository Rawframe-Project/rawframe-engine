#!/usr/bin/env python3
# Clicks in a client's window as a user would (D421): once the screen has
# been lit for two seconds (a server started with a black root window, Xvfb
# -br), the mouse moves to each point given, in the window's pixels from its
# top left (which is the screen's: no window manager runs), and its left
# button is pressed there and let go, a second apart. XTest injects the
# events through the X server, so they reach the window as a real mouse's
# do. Before each press it prints how bright a patch of five by five
# pixels at the point was before the mouse came and after, and whether it
# grew lighter by 8 or more of 255 (D422). A point followed by `:` and
# lower-case text has the text typed after its click, a key at a time,
# then Return (D426). Then it waits for the process to end.
#
# usage: click.py <pid> <x>,<y>[:<text>] [<x>,<y>[:<text>]...]

import ctypes
import ctypes.util
import os
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
    points = []
    for argument in sys.argv[2:]:
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
    lit = 0
    while alive(pid) and lit < 8:
        time.sleep(0.25)
        image = x.XGetImage(display, root, 0, 0, 64, 64, 0xFFFFFFFF, 2)
        if image:
            info = image.contents
            data = ctypes.cast(info.data, ctypes.POINTER(ctypes.c_ubyte * (info.bytes_per_line * info.height))).contents
            lit += 1 if any(bytes(data)) else 0
    for at, y, text in points:
        if not alive(pid):
            break
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
                code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
                xtest.XTestFakeKeyEvent(display, code, 1, 0)
                x.XFlush(display)
                time.sleep(0.05)
                xtest.XTestFakeKeyEvent(display, code, 0, 0)
                x.XFlush(display)
                time.sleep(0.1)
            print(f"typed {text} at {at},{y}")
            time.sleep(1)
    while alive(pid):
        time.sleep(0.25)


if __name__ == "__main__":
    main()
