#!/usr/bin/env python3
# Watches the X server's screen while a client runs (D280): every quarter
# second it takes the top left 1280 by 720 pixels of the screen, a server
# started with a black root window (Xvfb -br), and counts those not black,
# until the process ends. It prints the most counted in one picture, and
# writes that picture as a TGA file when asked. Asked to, once the screen
# has been lit for a second it resizes the window, as a user dragging its
# edge would (D281).
#
# usage: screen.py <pid> [picture.tga] [<width>x<height>]

import array
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
    out = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else None
    resize = tuple(int(side) for side in sys.argv[3].split("x")) if len(sys.argv) > 3 else None
    x = ctypes.CDLL(ctypes.util.find_library("X11"))
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XDefaultRootWindow.restype = ctypes.c_ulong
    x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x.XGetImage.restype = ctypes.POINTER(XImage)
    x.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                            ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
    display = x.XOpenDisplay(None)
    if not display:
        sys.exit("screen.py: no X display")
    root = x.XDefaultRootWindow(display)
    x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                             ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                             ctypes.POINTER(ctypes.c_uint)]
    x.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
    x.XFlush.argtypes = [ctypes.c_void_p]
    most, best, lit = 0, None, 0
    while alive(pid):
        time.sleep(0.25)
        image = x.XGetImage(display, root, 0, 0, 1280, 720, 0xFFFFFFFF, 2)
        if not image:
            continue
        info = image.contents
        rows = bytes(ctypes.cast(info.data, ctypes.POINTER(ctypes.c_ubyte * (info.bytes_per_line * info.height))).contents)
        counted = 0
        for row in range(info.height):
            line = array.array("I", rows[row * info.bytes_per_line:row * info.bytes_per_line + info.width * 4])
            counted += sum(1 for pixel in line if pixel & 0xFFFFFF)
        if counted > most:
            most, best = counted, (info.width, info.height, info.bytes_per_line, rows)
        lit += 1 if counted else 0
        if resize and lit == 4:
            # No window manager runs: the client's window is a child of the
            # root, the only one mapped.
            parent, top, children, count = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
            x.XQueryTree(display, root, ctypes.byref(top), ctypes.byref(parent), ctypes.byref(children), ctypes.byref(count))
            for index in range(count.value):
                x.XResizeWindow(display, children[index], resize[0], resize[1])
            x.XFlush(display)
            print(f"resized {count.value} window(s) to {resize[0]}x{resize[1]}")
    print(f"the screen showed {most} lit pixels at most")
    if out and best:
        width, height, pitch, rows = best
        header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, width & 255, width >> 8, height & 255, height >> 8, 32, 0x28])
        with open(out, "wb") as file:
            file.write(header)
            for row in range(height):
                file.write(rows[row * pitch:row * pitch + width * 4])


if __name__ == "__main__":
    main()
