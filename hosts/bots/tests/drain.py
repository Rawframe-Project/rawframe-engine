"""A dedicated server drains (SPEC-0012): asked to stop while bots play, it
tells them it is stopping, closes admission, refuses a second bots process
as unavailable, keeps serving the first until those bots leave, and only
then stops, well within its drain time. Run from the repository root.

A stop is asked the way each system asks: SIGTERM on POSIX, Ctrl+Break to
the process's own group on Windows (D237), which is why this is Python and
not a shell script, whose kill cannot send the second.

usage: drain.py <rawframe-server> <rawframe-bots>
"""
import os
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time

WINDOWS = os.name == "nt"


def start(program, config, log):
    """A process of the engine's, its output to `log`, in a group of its own
    on Windows so a Ctrl+Break reaches it alone."""
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if WINDOWS else 0
    return subprocess.Popen([program, "--config", str(config)], stdout=log, stderr=subprocess.STDOUT,
                            creationflags=flags)


def ask_to_stop(process):
    process.send_signal(signal.CTRL_BREAK_EVENT if WINDOWS else signal.SIGTERM)


def await_text(log, text):
    """Until a log holds `text`, or fails after thirty seconds."""
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if log.exists() and text in log.read_text(errors="replace"):
            return
        time.sleep(0.05)
    sys.exit(f"timed out waiting for {text} in {log}")


def main(server, bots):
    here = pathlib.Path.cwd().as_posix()
    with tempfile.TemporaryDirectory() as directory:
        work = pathlib.Path(directory)
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        fingerprint = (work / "fingerprint").as_posix()
        (work / "server.conf").write_text(
            "host.iteration_rate = 120\n"
            "host.drain_ms = 60000\n"
            "world.tick_rate = 60\n"
            f"kest.game = {here}/games/arena/arena.game\n"
            "network.quic.self_signed = true\n"
            f"network.quic.fingerprint_file = {fingerprint}\n"
            f"replication.endpoint = 127.0.0.1:{port}\n")

        def bots_conf(name, iterations):
            (work / name).write_text(
                f"host.maximum_iterations = {iterations}\n"
                "host.iteration_rate = 120\n"
                "kest.plan_only = true\n"
                f"kest.game = {here}/games/arena/arena.game\n"
                f"network.quic.pin_file = {fingerprint}\n"
                "bots.count = 2\n"
                f"bots.endpoint = 127.0.0.1:{port}\n")
            return work / name

        playing_conf = bots_conf("playing.conf", 0)
        late_conf = bots_conf("late.conf", 360)
        logs = {name: work / f"{name}.log" for name in ("server", "playing", "late")}
        opened = {name: open(path, "wb") for name, path in logs.items()}
        running = []
        try:
            server_process = start(server, work / "server.conf", opened["server"])
            running.append(server_process)
            await_text(logs["server"], '"code":"listening"')

            # The first bots play until they are told to stop.
            playing = start(bots, playing_conf, opened["playing"])
            running.append(playing)
            await_text(logs["playing"], '"code":"bots_admitted"')

            # The server drains, the bots still connected.
            ask_to_stop(server_process)
            await_text(logs["server"], '"state":"draining"')
            # A second request changes nothing.
            ask_to_stop(server_process)

            # Bots arriving now are refused.
            start(bots, late_conf, opened["late"]).wait(timeout=60)

            # The server is still serving the first bots; once they leave, it
            # stops.
            time.sleep(0.2)
            still_serving = server_process.poll() is None
            ask_to_stop(playing)
            playing.wait(timeout=60)
            server_exit = server_process.wait(timeout=60)
            running.clear()
        finally:
            for process in running:
                process.kill()
            for log in opened.values():
                log.close()
        text = {name: path.read_text(errors="replace") for name, path in logs.items()}
        for name in ("server", "playing", "late"):
            sys.stdout.write(text[name])
        checks = [
            ("the server served on while draining", still_serving),
            ("the server exited 0", server_exit == 0),
            ("the server drained", '"reason":"drained"' in text["server"]),
            ("two admissions refused", '"admissionsRefused":2' in text["server"]),
            ("the late bots were unavailable", '"bots":2,"admitted":0,"unavailable":2' in text["late"]),
            ("the playing bots were told",
             '"bots":2,"admitted":2,"unavailable":0,"serverStopping":2' in text["playing"]),
        ]
        failed = [what for what, held in checks if not held]
        if failed:
            sys.exit("drain failed: " + ", ".join(f"expected {what}" for what in failed))
        print("drained in order")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
