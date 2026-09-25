#!/usr/bin/env python3
"""Makes the localization module's CLDR tables (ADR-0050).

Reads the pinned CLDR data in third_party/cldr, never the network, and
writes C++ tables under modules/localization/src/generated/, formatted as
the repository formats its sources. Run after tools/update_cldr.sh, and
commit what it writes; nothing reads CLDR's files at build or run time.

    tools/generate_cldr.py
"""

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLDR = ROOT / "third_party" / "cldr" / "cldr-json"
OUT = ROOT / "modules" / "localization" / "src" / "generated"

LANGUAGE = re.compile(r"^[a-z]{2,3}$")
SCRIPT = re.compile(r"^[A-Z][a-z]{3}$")
REGION = re.compile(r"^([A-Z]{2}|[0-9]{3})$")
TAG = re.compile(r"^[a-z]{2,3}(-[A-Z][a-z]{3})?(-([A-Z]{2}|[0-9]{3}))?$")


def supplemental(name):
    return json.loads((CLDR / "cldr-core" / "supplemental" / f"{name}.json").read_text(encoding="utf-8"))["supplemental"]


def literal(text):
    return json.dumps(text, ensure_ascii=False)


def pairs(name, entries, comment):
    rows = "\n".join(f"    Pair{{{literal(a)}, {literal(b)}}}," for a, b in sorted(entries))
    return (
        f"// {comment}\n"
        f"constexpr std::array<Pair, {len(entries)}> k{name}{{{{\n{rows}\n}}}};\n\n"
        f"std::span<const Pair> {name[0].lower() + name[1:]}() noexcept {{\n    return k{name};\n}}\n"
    )


def words(name, entries, comment):
    rows = "\n".join(f"    std::string_view{{{literal(a)}}}," for a in sorted(entries))
    return (
        f"// {comment}\n"
        f"constexpr std::array<std::string_view, {len(entries)}> k{name}{{{{\n{rows}\n}}}};\n\n"
        f"std::span<const std::string_view> {name[0].lower() + name[1:]}() noexcept {{\n    return k{name};\n}}\n"
    )


def locales():
    likely = {}
    for key, value in supplemental("likelySubtags")["likelySubtags"].items():
        if TAG.match(key) and TAG.match(value) and value.count("-") == 2:
            likely[key] = value
    parents = {}
    for key, value in supplemental("parentLocales")["parentLocales"]["parentLocale"].items():
        if TAG.match(key) and (TAG.match(value) or value == "und"):
            parents[key] = value
    alias = supplemental("aliases")["metadata"]["alias"]
    languages = {}
    for key, value in alias["languageAlias"].items():
        replacement = value["_replacement"]
        if LANGUAGE.match(key) and TAG.match(replacement):
            languages[key] = replacement
    scripts = {k: v["_replacement"] for k, v in alias["scriptAlias"].items() if SCRIPT.match(k) and SCRIPT.match(v["_replacement"])}
    regions = {}
    for key, value in alias["territoryAlias"].items():
        first = value["_replacement"].split()[0]
        if REGION.match(key) and REGION.match(first):
            regions[key] = first
    known = {"language": set(), "script": set(), "region": set()}
    for tag in list(likely) + list(likely.values()):
        for part in tag.split("-"):
            kind = "language" if LANGUAGE.match(part) else "script" if SCRIPT.match(part) else "region"
            known[kind].add(part)
    body = "\n".join(
        [
            pairs("LikelySubtags", likely.items(), "Likely subtags: a tag to its maximal form."),
            pairs("ParentLocales", parents.items(), "Parent locale overrides; `und` is the root."),
            pairs("LanguageAliases", languages.items(), "A language's replacement, which may add a script or region."),
            pairs("ScriptAliases", scripts.items(), "A script's replacement."),
            pairs("RegionAliases", regions.items(), "A region's replacement, the first where CLDR gives several."),
            words("Languages", known["language"], "Every language the likely subtags name."),
            words("Scripts", known["script"], "Every script the likely subtags name."),
            words("Regions", known["region"], "Every region the likely subtags name."),
        ]
    )
    return body


def write(name, body):
    version = supplemental("plurals")["version"]["_cldrVersion"]
    text = (
        f"// Made by tools/generate_cldr.py from CLDR {version}; do not edit.\n\n"
        '#include "../cldr.h"\n\n#include <array>\n#include <span>\n#include <string_view>\n\n'
        "namespace rawframe::localization::cldr {\n\n"
        f"{body}\n"
        "} // namespace rawframe::localization::cldr\n"
    )
    path = OUT / name
    path.write_text(text, encoding="utf-8")
    subprocess.run(["clang-format-20", "-i", str(path)], check=True)
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write("locales.cpp", locales())
    return 0


if __name__ == "__main__":
    sys.exit(main())
