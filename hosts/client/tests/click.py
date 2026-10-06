#!/usr/bin/env python3
# Clicks in a client's window as a user would (D421): once the screen has
# been lit for two seconds (a server started with a black root window, Xvfb
# -br), the mouse moves to each point given, in the window's pixels from its
# top left (which is the screen's: no window manager runs), and its left
# button is pressed there and let go, a second apart. XTest injects the
# events through the X server, so they reach the window as a real mouse's
# do. It waits for the process to end, then prints what it clicked.
#
# usage: click.py <pid> <x>,<y> [<x>,<y>...]

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


def main():
    pid = int(sys.argv[1])
    points = [tuple(int(side) for side in point.split(",")) for point in sys.argv[2:]]
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
    for at, y in points:
        if not alive(pid):
            break
        # Moved there first, so the press is where the pointer already is.
        xtest.XTestFakeMotionEvent(display, -1, at, y, 0)
        x.XFlush(display)
        time.sleep(0.5)
        xtest.XTestFakeButtonEvent(display, 1, 1, 0)
        x.XFlush(display)
        time.sleep(0.2)
        xtest.XTestFakeButtonEvent(display, 1, 0, 0)
        x.XFlush(display)
        print(f"clicked at {at},{y}")
        time.sleep(1)
    while alive(pid):
        time.sleep(0.25)


if __name__ == "__main__":
    main()
