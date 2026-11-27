#!/usr/bin/env python3
"""Reads a host's newline-delimited JSON log and prints what a reviewer of
the run needs: every record at warning or above, and every summary record,
one compact line each. Exits 1 when the program failed or logged an error.

usage: summarize.py [--status N] [--stderr FILE] LOG
       summarize.py --png TGA PNG

--png converts an uncompressed TGA capture to PNG, so an agent can look
at the frame; it needs nothing beyond the standard library.
"""

import json
import struct
import sys
import zlib

WIDTH = 400


def compact(record):
    fields = record.get("fields") or {}
    text = json.dumps(fields, separators=(",", ":"), sort_keys=False)
    message = record.get("message")
    head = f"{record.get('domain')}.{record.get('code')}"
    body = f"{message} {text}" if message else text
    line = f"{head} {body}"
    return line if len(line) <= WIDTH else line[: WIDTH - 3] + "..."


def summarize(path, status, stderr_path):
    loud = {}
    summaries = []
    unreadable = 0
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                unreadable += 1
                continue
            severity = record.get("severity")
            if severity in ("warning", "error", "critical"):
                key = (severity, record.get("domain"), record.get("code"))
                loud.setdefault(key, []).append(record)
            if str(record.get("code", "")).endswith("_summary"):
                summaries.append(record)

    failed = status != 0
    print(f"== exit {status}")
    if stderr_path:
        with open(stderr_path, encoding="utf-8", errors="replace") as err:
            text = err.read().strip()
        if text:
            print("== standard error")
            print("\n".join(text.splitlines()[-20:]))
    order = {"critical": 0, "error": 1, "warning": 2}
    if loud:
        print("== warnings and errors")
        for (severity, _, _), records in sorted(loud.items(), key=lambda item: order[item[0][0]]):
            if severity != "warning":
                failed = True
            more = f" (x{len(records)})" if len(records) > 1 else ""
            print(f"{severity.upper()}{more} {compact(records[0])}")
    else:
        print("== no warnings or errors")
    print("== summaries")
    for record in summaries:
        print(compact(record))
    if not summaries:
        print("(none: the run stopped before its summaries)")
        failed = True
    if unreadable:
        print(f"== {unreadable} lines were not JSON records")
    print("== verdict: " + ("FAILED" if failed else "clean"))
    return 1 if failed else 0


def tga_to_png(source, target):
    data = open(source, "rb").read()
    if data[2] != 2:
        sys.exit(f"{source}: only uncompressed true-color TGA is read")
    width, height = struct.unpack("<HH", data[12:16])
    step = data[16] // 8
    from_top = data[17] & 0x20
    pixels = data[18 + data[0]:]
    rows = []
    for y in range(height):
        row = pixels[y * width * step:(y + 1) * width * step]
        out = bytearray(b"\x00")
        for x in range(width):
            b, g, r = row[x * step:x * step + 3]
            out += bytes((r, g, b))
        rows.append(bytes(out))
    if not from_top:
        rows.reverse()

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(target, "wb") as png:
        png.write(b"\x89PNG\r\n\x1a\n")
        png.write(chunk(b"IHDR", header))
        png.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 6)))
        png.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) == 4 and argv[1] == "--png":
        tga_to_png(argv[2], argv[3])
        return 0
    status = 0
    stderr_path = None
    rest = argv[1:]
    while rest and rest[0].startswith("--"):
        if rest[0] == "--status" and len(rest) > 1:
            status = int(rest[1])
        elif rest[0] == "--stderr" and len(rest) > 1:
            stderr_path = rest[1]
        else:
            sys.exit(__doc__)
        rest = rest[2:]
    if len(rest) != 1:
        sys.exit(__doc__)
    return summarize(rest[0], status, stderr_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
