#!/usr/bin/env python3
# A headset player's right hand at Reach's table, over and over: down onto
# a block of the near row, a grip, up, a swing toward the far side, and
# open while still moving, so the block is taken and thrown from the
# headset end to end. Rounds take turns: one grips with the controller's
# trigger on the row's middle block (D662), the next with the tracked hand
# closing into a fist on its left block (D664), the trigger left alone. It
# drives Monado's remote driver (tools/xr_run.sh --hands), whose Valve
# Index controllers a debugger places and presses over a socket, and whose
# hands it tracks from the curls it is told: the remote hub sends its first
# and latest state as it accepts, then reads one state a write, kept until
# the next.
#
# The hand's places are in the first seat's frame as Reach's sample makes
# them (x to the right, y up from the floor, -z ahead), the head where
# Monado puts it at the start and the seated eye at 1.2 meters (sample.kest).
#
# usage: reach_hands.py <port> <ready file, written once the first state is>

import os
import socket
import struct
import sys
import time

# The hub's state, Monado 21's `struct r_remote_data`: a header, the head
# (two views, its center, a flag), and the left and right controllers.
SIZE = 376
HEADER = b"mndrmt3\0"
HEAD_CENTER = 104
LEFT = 136
RIGHT = 256
# Within a controller: its pose (orientation then position), its hand's
# five curls, the trigger's value, and its flags: whether its hand is
# tracked, `active`, and the trigger's click among them.
POSITION = 16
CURLS = 52
TRIGGER_VALUE = 72
HAND_TRACKED = 104
ACTIVE = 105
TRIGGER_CLICK = 112
TRIGGER_TOUCH = 113

EYE = 1.2
# Where the hand is (seat frame, before the round's block is chosen across)
# and whether it grips, by seconds into a three-second round; between two,
# it moves straight. The trigger's rounds reach for the block at 0, the
# fist's for the block at -0.3.
ROUND = 3.0
ACROSS = (0.0, -0.3)
WAYPOINTS = [
    (0.0, (0.0, 1.1, -0.75), False),
    (0.6, (0.0, 0.88, -0.75), False),
    (0.9, (0.0, 0.88, -0.75), True),
    (1.4, (0.0, 1.4, -0.75), True),
    (1.9, (0.0, 1.4, -1.45), True),
    (1.91, (0.0, 1.4, -1.47), False),
    (2.1, (0.0, 1.4, -1.6), False),
    (3.0, (0.0, 1.1, -0.75), False),
]


def connected(port):
    for _ in range(200):
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError:
            time.sleep(0.05)
    sys.exit("reach_hands.py: Monado's remote hub never answered on port %d" % port)


def received(link, size):
    data = b""
    while len(data) < size:
        more = link.recv(size - len(data))
        if not more:
            sys.exit("reach_hands.py: the hub closed before its state")
        data += more
    return data


def hand_at(moment):
    for (begun, start, gripped), (ends, end, _) in zip(WAYPOINTS, WAYPOINTS[1:]):
        if moment < ends:
            along = (moment - begun) / (ends - begun)
            return tuple(a + (b - a) * along for a, b in zip(start, end)), gripped
    return WAYPOINTS[-1][1], WAYPOINTS[-1][2]


def main():
    link = connected(int(sys.argv[1]))
    # The first state, then the latest, which is written back changed.
    received(link, SIZE)
    state = bytearray(received(link, SIZE))
    state[0:8] = HEADER
    head = struct.unpack_from("<3f", state, HEAD_CENTER + POSITION)
    # The left hand is never tracked: only the right's fist closes.
    state[LEFT + HAND_TRACKED] = 0
    begun = time.monotonic()
    while True:
        played = time.monotonic() - begun
        fist = int(played // ROUND) % 2 == 1
        (x, y, z), gripped = hand_at(played % ROUND)
        x += ACROSS[1 if fist else 0]
        struct.pack_into("<3f", state, RIGHT + POSITION, head[0] + x, head[1] + y - EYE, head[2] + z)
        pressed = gripped and not fist
        struct.pack_into("<f", state, RIGHT + TRIGGER_VALUE, 1.0 if pressed else 0.0)
        state[RIGHT + ACTIVE] = 1
        state[RIGHT + TRIGGER_CLICK] = 1 if pressed else 0
        state[RIGHT + TRIGGER_TOUCH] = 1 if pressed else 0
        state[RIGHT + HAND_TRACKED] = 1 if fist else 0
        struct.pack_into("<5f", state, RIGHT + CURLS, *([1.0 if gripped and fist else 0.0] * 5))
        try:
            link.sendall(state)
        except OSError:
            return
        if not os.path.exists(sys.argv[2]):
            open(sys.argv[2], "w").close()
        time.sleep(0.01)


main()
