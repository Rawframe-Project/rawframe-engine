#!/usr/bin/env python3
# Drops files on a window as a file manager does (D802), by XDND 5 over the
# X server the tests run: a source window of its own offers text/uri-list,
# enters the window under the point, moves there, and drops once the window
# says it takes the drop; the window then asks for the list, which is given
# it, and says when it finished. Used by click.py's `drop=` and alone:
#
#   xdnd.py <x> <y> <file>...   the point in the screen's pixels
import ctypes
import ctypes.util
import os
import sys
import time
import urllib.parse

xlib = ctypes.CDLL(ctypes.util.find_library("X11"))
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XDefaultRootWindow.restype = ctypes.c_ulong
xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
xlib.XInternAtom.restype = ctypes.c_ulong
xlib.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
xlib.XCreateSimpleWindow.restype = ctypes.c_ulong
xlib.XCreateSimpleWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                                     ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_ulong]
xlib.XSetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
xlib.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                                       ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                                       ctypes.POINTER(ctypes.c_ulong)]
xlib.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long, ctypes.c_long,
                                    ctypes.c_int, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong),
                                    ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_void_p)]
xlib.XFree.argtypes = [ctypes.c_void_p]
xlib.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.c_void_p]
xlib.XChangeProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
xlib.XPending.argtypes = [ctypes.c_void_p]
xlib.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
xlib.XFlush.argtypes = [ctypes.c_void_p]
xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]

CLIENT_MESSAGE = 33
SELECTION_REQUEST = 30
SELECTION_NOTIFY = 31
PROP_MODE_REPLACE = 0


class ClientMessage(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                ("format", ctypes.c_int), ("data", ctypes.c_long * 5)]


class SelectionRequest(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p), ("owner", ctypes.c_ulong), ("requestor", ctypes.c_ulong),
                ("selection", ctypes.c_ulong), ("target", ctypes.c_ulong), ("property", ctypes.c_ulong),
                ("time", ctypes.c_ulong)]


class SelectionNotify(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p), ("requestor", ctypes.c_ulong), ("selection", ctypes.c_ulong),
                ("target", ctypes.c_ulong), ("property", ctypes.c_ulong), ("time", ctypes.c_ulong)]


class Event(ctypes.Union):
    _fields_ = [("type", ctypes.c_int), ("client", ClientMessage), ("request", SelectionRequest),
                ("pad", ctypes.c_long * 24)]


def aware(display, window, atom):
    """Whether `window` says it takes XDND drops."""
    kind = ctypes.c_ulong()
    form = ctypes.c_int()
    count = ctypes.c_ulong()
    after = ctypes.c_ulong()
    data = ctypes.c_void_p()
    xlib.XGetWindowProperty(display, window, atom, 0, 1, 0, 0, ctypes.byref(kind), ctypes.byref(form),
                            ctypes.byref(count), ctypes.byref(after), ctypes.byref(data))
    if data.value:
        xlib.XFree(data)
    return count.value > 0


def target_at(display, root, x, y, xdnd_aware):
    """The deepest window under the point that takes drops, if any."""
    window = root
    found = 0
    while True:
        child = ctypes.c_ulong()
        tx = ctypes.c_int()
        ty = ctypes.c_int()
        xlib.XTranslateCoordinates(display, root, window, x, y, ctypes.byref(tx), ctypes.byref(ty),
                                   ctypes.byref(child))
        if not child.value:
            return found
        window = child.value
        if aware(display, window, xdnd_aware):
            found = window


def drop(x, y, paths, wait=10.0):
    """Drops `paths` at (x, y); true once the window says it finished."""
    display = xlib.XOpenDisplay(None)
    if not display:
        print("xdnd: no display")
        return False
    atom = lambda name: xlib.XInternAtom(display, name.encode(), 0)
    names = {name: atom(name) for name in ("XdndAware", "XdndEnter", "XdndPosition", "XdndStatus", "XdndDrop",
                                           "XdndFinished", "XdndLeave", "XdndSelection", "XdndActionCopy",
                                           "text/uri-list")}
    root = xlib.XDefaultRootWindow(display)
    target = target_at(display, root, x, y, names["XdndAware"])
    if not target:
        print("xdnd: no window takes drops at %d,%d" % (x, y))
        xlib.XCloseDisplay(display)
        return False
    source = xlib.XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0)
    xlib.XSetSelectionOwner(display, names["XdndSelection"], source, 0)
    listed = "".join("file://" + urllib.parse.quote(os.path.abspath(path)) + "\r\n" for path in paths).encode()

    def send(kind, data):
        message = Event()
        message.client.type = CLIENT_MESSAGE
        message.client.window = target
        message.client.message_type = names[kind]
        message.client.format = 32
        for at, value in enumerate(data):
            message.client.data[at] = value
        xlib.XSendEvent(display, target, 0, 0, ctypes.byref(message))
        xlib.XFlush(display)

    send("XdndEnter", [source, 5 << 24, names["text/uri-list"], 0, 0])
    send("XdndPosition", [source, 0, (x << 16) | y, 0, names["XdndActionCopy"]])
    dropped = False
    finished = False
    until = time.monotonic() + wait
    event = Event()
    while time.monotonic() < until and not finished:
        if not xlib.XPending(display):
            time.sleep(0.02)
            continue
        xlib.XNextEvent(display, ctypes.byref(event))
        if event.type == CLIENT_MESSAGE and event.client.message_type == names["XdndStatus"]:
            if event.client.data[1] & 1 and not dropped:
                send("XdndDrop", [source, 0, 0, 0, 0])
                dropped = True
            elif not dropped:
                # Not taken here: move again, as a hand would.
                time.sleep(0.1)
                send("XdndPosition", [source, 0, (x << 16) | y, 0, names["XdndActionCopy"]])
        elif event.type == SELECTION_REQUEST:
            asked = event.request
            reply = SelectionNotify(type=SELECTION_NOTIFY, send_event=1, requestor=asked.requestor,
                                    selection=asked.selection, target=asked.target, property=0, time=asked.time)
            if asked.target == names["text/uri-list"]:
                xlib.XChangeProperty(display, asked.requestor, asked.property, asked.target, 8, PROP_MODE_REPLACE,
                                     listed, len(listed))
                reply.property = asked.property
            xlib.XSendEvent(display, asked.requestor, 0, 0, ctypes.byref(reply))
            xlib.XFlush(display)
        elif event.type == CLIENT_MESSAGE and event.client.message_type == names["XdndFinished"]:
            finished = True
    if not dropped:
        send("XdndLeave", [source, 0, 0, 0, 0])
    xlib.XSync(display, 0)
    xlib.XCloseDisplay(display)
    print("dropped %s at %d,%d: %s" % (" ".join(paths), x, y, "finished" if finished else "not finished"))
    return finished


if __name__ == "__main__":
    sys.exit(0 if drop(int(sys.argv[1]), int(sys.argv[2]), sys.argv[3:]) else 1)
