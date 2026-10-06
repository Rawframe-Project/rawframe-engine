#!/usr/bin/env python3
# Prints a UDP port on 127.0.0.1 that nothing holds now, for a test's server
# to listen on. Not the system's pick: binding port 0 hands out one of the
# ports outgoing connections are given (from 32768 on Linux, 49152 on
# Windows and macOS), which a client starting meanwhile may take before the
# server binds it. These come from 30000 to 32767 instead, above the window
# exported games take theirs from (20000 to 29999, D402), at random so
# tests running at once rarely reach for the same one, and checked free.

import random
import socket
import sys

for _ in range(200):
    port = random.randint(30000, 32767)
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.bind(("127.0.0.1", port))
    except OSError:
        continue
    finally:
        probe.close()
    print(port)
    sys.exit(0)
sys.exit("free_port.py: no free port from 30000 to 32767")
