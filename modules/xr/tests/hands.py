#!/usr/bin/env python3
# The hands a tracked-source test reads (D663): Monado's remote driver
# (tools/xr_run.sh --hands) told that its right controller's hand is
# tracked, open (every finger's curl nought), where the driver holds the
# controller; its left hand is not tracked. The hub sends its first and
# latest state as it accepts, then reads one state a write.
#
# usage: hands.py <port>

import socket
import sys
import time

# Monado 21's `struct r_remote_data`: its size, header, and the right
# controller's place; within a controller, whether its hand is tracked, and
# whether it is active.
SIZE = 376
HEADER = b"mndrmt3\0"
LEFT = 136
RIGHT = 256
HAND_TRACKED = 104
ACTIVE = 105


def connected(port):
    for _ in range(200):
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError:
            time.sleep(0.05)
    sys.exit("hands.py: Monado's remote hub never answered on port %d" % port)


def received(link, size):
    data = b""
    while len(data) < size:
        more = link.recv(size - len(data))
        if not more:
            sys.exit("hands.py: the hub closed before its state")
        data += more
    return data


def main():
    link = connected(int(sys.argv[1]))
    received(link, SIZE)
    state = bytearray(received(link, SIZE))
    state[0:8] = HEADER
    state[LEFT + HAND_TRACKED] = 0
    state[RIGHT + HAND_TRACKED] = 1
    state[LEFT + ACTIVE] = 1
    state[RIGHT + ACTIVE] = 1
    while True:
        try:
            link.sendall(state)
        except OSError:
            return
        time.sleep(0.01)


main()
