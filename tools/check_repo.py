#!/usr/bin/env python3
"""Repository rules the compiler cannot see.

1. Module boundaries: a module's sources include only its own headers and the
   headers of modules on its line in tools/modules.txt.
2. File size (STD-0001): handwritten source fails at 1,500 lines and is
   reported from 1,000.
3. Owner rules: no em dash in any tracked text file, and no AI attribution
   trailer in any commit message.
4. No `.value()` call in engine source: on a Result it throws on failure, and
   exceptions are disabled (SPEC-0004, SPEC-0050). Test for `has_value()` and
   dereference, or use RAWFRAME_TRY_ASSIGN.

Exits non-zero on any failure and prints one line per finding.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".c"}
FAIL_LINES = 1500
REPORT_LINES = 1000
EM_DASH = chr(0x2014)
ATTRIBUTION = re.compile(r"co-authored-by:|generated with|claude-session:", re.IGNORECASE)
VALUE_CALL = re.compile(r"\.value\(\s*\)")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]rawframe/([a-z_]+)/', re.MULTILINE)


def tracked_files():
    listing = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True)
    return [ROOT / name for name in listing.stdout.decode().split("\0") if name]


def read_modules():
    allowed = {}
    for line in (ROOT / "tools" / "modules.txt").read_text().splitlines():
        match = re.match(r"^([a-z_]+):(.*)$", line)
        if match:
            allowed[match.group(1)] = set(match.group(2).split())
    return allowed


def check_boundaries(files, allowed, findings):
    for path in files:
        relative = path.relative_to(ROOT)
        if relative.parts[0] != "modules" or path.suffix not in SOURCE_SUFFIXES:
            continue
        module = relative.parts[1]
        if module not in allowed:
            findings.append(f"{relative}: module '{module}' is not listed in tools/modules.txt")
            continue
        permitted = allowed[module] | {module}
        # Tests may also use the harness.
        if len(relative.parts) > 2 and relative.parts[2] == "tests":
            permitted = permitted | {"test"}
        for included in INCLUDE.findall(path.read_text(errors="replace")):
            if included not in permitted:
                findings.append(f"{relative}: includes rawframe/{included}/, not allowed for module '{module}'")


def check_value_calls(files, findings):
    for path in files:
        relative = path.relative_to(ROOT)
        if relative.parts[0] != "modules" or path.suffix not in SOURCE_SUFFIXES:
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if VALUE_CALL.search(line.split("//")[0]):
                findings.append(f"{relative}:{number}: .value() call; test has_value() and dereference instead")


def check_sizes(files, findings, notes):
    for path in files:
        if path.suffix not in SOURCE_SUFFIXES or "third_party" in path.parts:
            continue
        lines = path.read_text(errors="replace").count("\n")
        relative = path.relative_to(ROOT)
        if lines >= FAIL_LINES:
            findings.append(f"{relative}: {lines} lines, the limit is {FAIL_LINES} (STD-0001)")
        elif lines >= REPORT_LINES:
            notes.append(f"{relative}: {lines} lines, stop adding unrelated behavior (STD-0001)")


def check_owner_rules(files, findings):
    for path in files:
        try:
            text = path.read_text()
        except (UnicodeDecodeError, IsADirectoryError):
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if EM_DASH in line:
                findings.append(f"{path.relative_to(ROOT)}:{number}: em dash")
    log = subprocess.run(["git", "log", "--format=%H%n%B%n--end--"], cwd=ROOT, capture_output=True, text=True)
    commit = None
    for line in log.stdout.splitlines():
        if re.fullmatch(r"[0-9a-f]{40}", line):
            commit = line[:12]
        elif ATTRIBUTION.search(line):
            findings.append(f"commit {commit}: AI attribution in message: {line.strip()}")
        elif EM_DASH in line:
            findings.append(f"commit {commit}: em dash in message")


def main():
    files = [path for path in tracked_files() if path.is_file()]
    findings, notes = [], []
    check_boundaries(files, read_modules(), findings)
    check_value_calls(files, findings)
    check_sizes(files, findings, notes)
    check_owner_rules(files, findings)
    for note in notes:
        print(f"note: {note}")
    for finding in findings:
        print(f"error: {finding}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
