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
import unicodedata
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
    """A C++ string literal of UTF-8 text; characters a reader cannot see,
    such as marks and spaces other than the space itself, are escaped."""
    made = ['"']
    for each in text:
        if each in '"\\':
            made.append("\\" + each)
        elif each != " " and (unicodedata.category(each)[0] in "CZ"):
            made.append(f"\\u{ord(each):04X}" if ord(each) <= 0xFFFF else f"\\U{ord(each):08X}")
        else:
            made.append(each)
    made.append('"')
    return "".join(made)


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


CATEGORIES = ["zero", "one", "two", "few", "many", "other"]
TOKEN = re.compile(r"\s*(\.\.|!=|=|%|,|[a-z]+|[0-9]+)")


def tokens(rule):
    made, at = [], 0
    rule = rule.strip()
    while at < len(rule):
        match = TOKEN.match(rule, at)
        if not match:
            raise ValueError(f"cannot read plural rule {rule!r} at {at}")
        made.append(match.group(1))
        at = match.end()
    return made


def compile_condition(rule):
    """A CLDR plural condition as a C++ expression over `o`."""
    words = tokens(rule)
    at = 0

    def take():
        nonlocal at
        at += 1
        return words[at - 1]

    def relation():
        operand = take()
        if operand not in "nivwfte" or len(operand) != 1:
            raise ValueError(f"unknown operand {operand!r} in {rule!r}")
        modulus = None
        if at < len(words) and words[at] == "%":
            take()
            modulus = take()
        compare = take()
        values = f"o.{'i' if operand == 'n' else operand}"
        if modulus is not None:
            values = f"({values} % {modulus}ULL)"
        ranges = []
        while True:
            low = take()
            if at < len(words) and words[at] == "..":
                take()
                high = take()
                # An unsigned value is never below nought.
                below = "" if int(low) == 0 else f"{values} >= {low}ULL && "
                ranges.append(f"({below}{values} <= {high}ULL)")
            else:
                ranges.append(f"{values} == {low}ULL")
            if at < len(words) and words[at] == ",":
                take()
                continue
            break
        held = " || ".join(ranges)
        if operand == "n":
            # n is an integer only when no fraction digit is other than
            # nought.
            held = f"o.t == 0 && ({held})"
        return f"({held})" if compare == "=" else f"!({held})"

    def conjunction():
        parts = [relation()]
        while at < len(words) and words[at] == "and":
            take()
            parts.append(relation())
        return " && ".join(parts)

    parts = [conjunction()]
    while at < len(words) and words[at] == "or":
        take()
        parts.append(conjunction())
    return " || ".join(f"({part})" for part in parts)


def plural_rules():
    rules = {}
    functions = []
    tables = {}
    for kind in ("cardinal", "ordinal"):
        data = supplemental("plurals" if kind == "cardinal" else "ordinals")[f"plurals-type-{kind}"]
        entries = []
        for locale, counts in data.items():
            if locale != "root" and not TAG.match(locale):
                continue
            body = []
            for category in CATEGORIES[:-1]:
                text = counts.get(f"pluralRule-count-{category}")
                if text is None:
                    continue
                condition = text.split("@")[0].strip()
                if condition:
                    body.append(f"    if ({compile_condition(condition)}) {{\n        return PluralCategory::{category.title()};\n    }}")
            body.append("    return PluralCategory::Other;")
            code = "\n".join(body)
            if code not in rules:
                rules[code] = f"rule{len(rules)}"
                functions.append(f"PluralCategory {rules[code]}([[maybe_unused]] const PluralOperands& o) noexcept {{\n{code}\n}}\n")
            entries.append((locale, rules[code]))
        tables[kind] = sorted(entries)
    out = ["namespace {\n", *functions, "} // namespace\n"]
    for kind, entries in tables.items():
        rows = "\n".join(f"    RuleSet{{{literal(locale)}, &{name}}}," for locale, name in entries)
        out.append(f"constexpr std::array<RuleSet, {len(entries)}> k{kind.title()}{{{{\n{rows}\n}}}};\n")
        out.append(f"std::span<const RuleSet> {kind}Rules() noexcept {{\n    return k{kind.title()};\n}}\n")
    return "\n".join(out)


def plural_samples():
    """CLDR's own examples of each category, the rules' conformance oracle."""
    rows = []
    for kind in ("cardinal", "ordinal"):
        data = supplemental("plurals" if kind == "cardinal" else "ordinals")[f"plurals-type-{kind}"]
        for locale, counts in sorted(data.items()):
            if locale != "root" and not TAG.match(locale):
                continue
            for category in CATEGORIES:
                text = counts.get(f"pluralRule-count-{category}")
                if text is None:
                    continue
                for group in text.split("@")[1:]:
                    for sample in group.split(maxsplit=1)[1].split(","):
                        sample = sample.strip()
                        if not sample or sample == "\u2026" or "c" in sample or "e" in sample:
                            continue
                        for end in sample.split("~"):
                            rows.append((locale, kind == "ordinal", end, category))
    lines = "\n".join(
        f"    PluralSample{{{literal(locale)}, {'true' if ordinal else 'false'}, {literal(number)}, PluralCategory::{category.title()}}},"
        for locale, ordinal, number, category in rows
    )
    return (
        f"constexpr std::array<PluralSample, {len(rows)}> kSamples{{{{\n{lines}\n}}}};\n\n"
        "std::span<const PluralSample> pluralSamples() noexcept {\n    return kSamples;\n}\n"
    )


def numbers():
    """Each modern locale's number symbols, digits, and grouping."""
    systems = supplemental("numberingSystems")["numberingSystems"]
    rows = []
    for path in sorted((CLDR / "cldr-numbers-full" / "main").glob("*/numbers.json")):
        locale = path.parent.name
        if not TAG.match(locale):
            continue
        data = json.loads(path.read_text(encoding="utf-8"))["main"][locale]["numbers"]
        system = data["defaultNumberingSystem"]
        symbols = data[f"symbols-numberSystem-{system}"]
        pattern = data[f"decimalFormats-numberSystem-{system}"]["standard"].split(";")[0]
        whole = pattern.split(".")[0]
        groups = whole.split(",")
        primary = len(groups[-1]) if len(groups) > 1 else 0
        secondary = len(groups[-2]) if len(groups) > 2 else primary
        digits = systems[system]["_digits"]
        if len(digits) != 10:
            raise ValueError(f"{locale}: {system} has no ten digits")
        rows.append(
            f"    NumberSymbols{{{literal(locale)}, {literal(symbols['decimal'])}, {literal(symbols['group'])}, "
            f"{literal(symbols['minusSign'])}, {literal(digits)}, {primary}, {secondary}, {int(data['minimumGroupingDigits'])}}},"
        )
    lines = "\n".join(rows)
    return (
        "// Each locale of modern coverage: its decimal and group separators, minus\n"
        "// sign, digits nought to nine, grouping sizes, and least digits grouped.\n"
        f"constexpr std::array<NumberSymbols, {len(rows)}> kNumbers{{{{\n{lines}\n}}}};\n\n"
        "std::span<const NumberSymbols> numberSymbols() noexcept {\n    return kNumbers;\n}\n"
    )


def write(name, body, header='"../cldr.h"', namespace="rawframe::localization::cldr", out=None):
    version = supplemental("plurals")["version"]["_cldrVersion"]
    text = (
        f"// Made by tools/generate_cldr.py from CLDR {version}; do not edit.\n\n"
        f"#include {header}\n\n#include <array>\n#include <span>\n#include <string_view>\n\n"
        f"namespace {namespace} {{\n\n"
        f"{body}\n"
        f"}} // namespace {namespace}\n"
    )
    path = (out or OUT) / name
    path.write_text(text, encoding="utf-8")
    subprocess.run(["clang-format-20", "-i", str(path)], check=True)
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write("locales.cpp", locales())
    write("plurals.cpp", plural_rules(), header='"../plural_rules.h"')
    write("numbers.cpp", numbers())
    samples = ROOT / "modules" / "localization" / "tests" / "generated"
    samples.mkdir(parents=True, exist_ok=True)
    write("plural_samples.cpp", plural_samples(), header='"../plural_samples.h"', namespace="rawframe::localization::oracle", out=samples)
    return 0


if __name__ == "__main__":
    sys.exit(main())
