#!/usr/bin/env bash
# Replaces third_party/cldr with the CLDR data localization compiles ahead of
# time (ADR-0050), from the cldr-json repository at one exact commit: the
# supplemental files for plural and ordinal rules, likely subtags, parent
# locales, aliases, numbering systems, and coverage, the number data of every
# locale of modern coverage, and the license. Files are upstream's bytes,
# unchanged; tools/generate_cldr.py makes the engine's tables from them.
#
#   tools/update_cldr.sh <commit>
set -euo pipefail
cd "$(dirname "$0")/.."

commit="$1"
if [[ ! "$commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo "update_cldr: a commit is 40 hex digits" >&2
    exit 1
fi
base="https://raw.githubusercontent.com/unicode-org/cldr-json/$commit"
target="third_party/cldr"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fetch() {
    mkdir -p "$work/$(dirname "$1")"
    curl --fail --silent --show-error --retry 3 -o "$work/$1" "$base/$1"
}
export -f fetch
export base work

fetch LICENSE
for file in plurals ordinals likelySubtags parentLocales aliases numberingSystems; do
    fetch "cldr-json/cldr-core/supplemental/$file.json"
done
fetch cldr-json/cldr-core/coverageLevels.json
python3 - "$work/cldr-json/cldr-core/coverageLevels.json" > "$work/locales" <<'PY'
import json, sys
levels = json.load(open(sys.argv[1]))["effectiveCoverageLevels"]
print("\n".join(sorted(locale for locale, level in levels.items() if level == "modern")))
PY
sed 's|.*|cldr-json/cldr-numbers-full/main/&/numbers.json|' "$work/locales" | xargs -P 8 -I{} bash -c 'fetch "$@"' _ {}
rm "$work/locales"

rm -rf "$target"
mkdir -p "$target"
cp -R "$work"/. "$target/"
echo "$target is now cldr-json $commit; run tools/generate_cldr.py and update the table in third_party/README.md"
