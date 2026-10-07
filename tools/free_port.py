#!/usr/bin/env python3
# Prints a UDP port on 127.0.0.1 that nothing holds now, for a test's server
# to listen on. Not the system's pick: binding port 0 hands out one of the
# ports outgoing connections are given (from 32768 on Linux, 49152 on
# Windows and macOS), which a client starting meanwhile may take before the
# server binds it. These come from 30000 to 32767 instead, above the window
# exported games take theirs from (20000 to 29999, D402), at random, and
# checked free. Each is also claimed for ten minutes in a directory the
# machine's tests share, a file made only if none is there, so tests running
# at once never hand out the same one: at random alone, two of a full
# check's servers took one port twice in a night (D491).

import os
import random
import socket
import sys
import tempfile
import time

CLAIMS = os.path.join(tempfile.gettempdir(), "rawframe-ports")
CLAIMED_FOR = 600


def claim(port):
    """Whether this caller now holds `port`'s claim: a fresh file made,
    or one older than the claim's life taken over. The directory is every
    user's, as the temporary directory is (CI runs as another user on the
    same machine); where no claim can be written, the port is handed out
    unclaimed, as before."""
    try:
        os.mkdir(CLAIMS)
        os.chmod(CLAIMS, 0o1777)
    except FileExistsError:
        pass
    except OSError:
        return True
    path = os.path.join(CLAIMS, str(port))
    try:
        os.close(os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644))
        return True
    except FileExistsError:
        try:
            if time.time() - os.path.getmtime(path) < CLAIMED_FOR:
                return False
            os.utime(path)
            return True
        except OSError:
            return False
    except OSError:
        return True


for _ in range(200):
    port = random.randint(30000, 32767)
    if not claim(port):
        continue
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
