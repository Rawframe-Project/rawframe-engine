#!/usr/bin/env python3
"""The tests a change can reach (D898), for the check's push tier.

    tools/affected.py <build tree> [<base>]

Prints the names of the tests in <build tree> that the change since <base>
(the merge base with origin/main unless named), together with the working
tree's own changes, can reach, one per line, or nothing when it reaches
none. A test is reached when its directory is a changed module's or host's,
or a module's or host's that depends on a changed module by the module
graph (tools/modules.txt), however indirectly. The cross-module scenarios
run whenever any module is reached. A module or host whose line in the graph
changed counts as changed, as does a directory the build's own file only
adds. A change the graph cannot place (the build's own files otherwise, a
vendored library, a game, a template, a test harness) reaches every test;
one that no test reads (the check's own scripts, documents) reaches none,
the full check after the push running everything (D898).
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Read by no test: the full check, run by the runner after every push,
# covers them.
UNREAD = re.compile(r"^(tools/(check\.sh|bench\.sh|web_budget\.sh|soak\.sh|affected\.py|package_studio\.sh)"
                    r"|tools/hooks/|\.github/|bench/|README\.md|CHANGELOG\.md|.*\.md$)")


def run(*arguments):
    return subprocess.run(arguments, cwd=ROOT, capture_output=True, text=True, check=False).stdout


def merge_base():
    return run("git", "merge-base", "HEAD", "origin/main").strip() or "HEAD"


def added_directories(base, directories):
    """Whether the build's own file changed only in the directories it adds,
    which are then changed themselves."""
    lines = [line for line in run("git", "diff", "-U0", base or merge_base(), "--", "CMakeLists.txt").splitlines()
             if line[:1] in "+-" and not line.startswith(("+++", "---"))]
    named = [re.match(r"^[+-]\s*add_subdirectory\(([\w/]+)\)\s*$", line) for line in lines]
    if not lines or not all(named):
        return False
    directories.update(match.group(1) for match in named)
    return True


def changed(base):
    """The paths changed since `base`, committed or not."""
    if not base:
        base = merge_base()
    paths = set(run("git", "diff", "--name-only", base).split())
    for line in run("git", "status", "--porcelain", "--untracked-files=all").splitlines():
        paths.add(line[3:].split(" -> ")[-1])
    return sorted(path for path in paths if path)


def graph():
    """Each module's and host's direct dependencies, by name."""
    depends = {}
    for line in (ROOT / "tools/modules.txt").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if ":" in line:
            name, _, deps = line.partition(":")
            depends[name.strip()] = set(deps.split())
    return depends


def hosts():
    """Each host program's name to the directory that declares it."""
    named = {}
    for listing in list(ROOT.glob("hosts/*/CMakeLists.txt")) + list(ROOT.glob("launcher/*/CMakeLists.txt")):
        for name in re.findall(r"rawframe_host\(NAME (\w+)", listing.read_text()):
            named[name] = listing.parent.relative_to(ROOT).as_posix()
    return named


def tests_in(tree, directory):
    listed = run("ctest", "--test-dir", str(tree / directory), "-N")
    return re.findall(r"^\s*Test\s+#\d+: (\S+)", listed, re.MULTILINE)


def main():
    tree = pathlib.Path(sys.argv[1])
    if not tree.is_absolute():
        tree = ROOT / tree
    base = sys.argv[2] if len(sys.argv) > 2 else ""
    depends = graph()
    programs = hosts()
    directories = set()
    reached = set()
    everything = False
    for path in changed(base):
        parts = path.split("/")
        if UNREAD.match(path):
            continue
        if parts[0] == "modules" and len(parts) > 1 and parts[1] in depends:
            reached.add(parts[1])
        elif parts[0] in ("hosts", "launcher") and len(parts) > 1:
            directories.add("/".join(parts[:2]))
        elif path == "tools/modules.txt":
            # A module or host whose line changed (a dependency added or
            # taken away, or the line new) is changed itself.
            for line in run("git", "diff", "-U0", base or merge_base(), "--", path).splitlines():
                named = re.match(r"^[+-](\w+):", line)
                if named:
                    reached.add(named.group(1))
        elif path == "CMakeLists.txt" and added_directories(base, directories):
            pass
        else:
            everything = True
    if everything:
        print("\n".join(tests_in(tree, ".")))
        return
    if not reached and not directories:
        return
    # Every module and host that depends on a reached module, to the top.
    grown = True
    while grown:
        grown = False
        for name, deps in depends.items():
            if name not in reached and deps & reached:
                reached.add(name)
                grown = True
    for name in reached:
        if (ROOT / "modules" / name).is_dir():
            directories.add(f"modules/{name}")
        if name in programs:
            directories.add(programs[name])
    if any(directory.startswith("modules/") for directory in directories):
        directories.add("tests/scenarios")
    names = []
    for directory in sorted(directories):
        if (tree / directory).is_dir():
            names.extend(tests_in(tree, directory))
    if names:
        print("\n".join(sorted(set(names))))


if __name__ == "__main__":
    main()
