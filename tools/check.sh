#!/usr/bin/env bash
# The one definition of "it passes".
#
#   tools/check.sh fast   repository rules, format, one incremental build, tests
#   tools/check.sh        the above plus GCC and Clang in every configuration,
#                         the address and thread sanitizers, the web build
#                         (wasm32 without threads, tests under Node), the web
#                         page against the dedicated server over WebTransport,
#                         and the tick budget (tools/bench.sh check) once all
#                         pass
#
# Build trees live under out/ and are reused, so a second run only rebuilds
# what changed.
set -euo pipefail
cd "$(dirname "$0")/.."

tier="${1:-full}"
start=$(date +%s)
failures=0

step() { printf '== %s\n' "$*"; }
fail() { printf 'FAILED: %s\n' "$*"; failures=$((failures + 1)); }

mkdir -p out

step "repository rules"
python3 tools/check_repo.py || fail "repository rules"

step "format"
mapfile -t sources < <(git ls-files '*.h' '*.cpp' | grep -v '^third_party/' || true)
if [ "${#sources[@]}" -gt 0 ]; then
    if ! clang-format-20 --dry-run --Werror "${sources[@]}" >out/format.log 2>&1; then
        head -20 out/format.log; fail "format (run: clang-format-20 -i on the files above)"
    fi
fi

build_and_test() {
    local preset="$1"
    step "$preset"
    if ! cmake --preset "$preset" >"out/$preset.configure.log" 2>&1; then
        tail -20 "out/$preset.configure.log"; fail "$preset configure"; return
    fi
    if ! cmake --build "out/$preset" >"out/$preset.build.log" 2>&1; then
        grep -E 'error|FAILED' "out/$preset.build.log" | head -20; fail "$preset build"; return
    fi
    if ! ctest --test-dir "out/$preset" --output-on-failure -j "$(nproc)" >"out/$preset.test.log" 2>&1; then
        tail -30 "out/$preset.test.log"; fail "$preset tests"; return
    fi
}

if [ "$tier" = "fast" ]; then
    build_and_test clang-development
else
    # Independent build trees, so they build in parallel.
    presets=(gcc-debug gcc-shipping clang-development clang-shipping clang-sanitize clang-thread wasm-development)
    pids=()
    for preset in "${presets[@]}"; do
        ( build_and_test "$preset" ) >"out/$preset.check.log" 2>&1 &
        pids+=($!)
    done
    for i in "${!presets[@]}"; do
        wait "${pids[$i]}" || true
        cat "out/${presets[$i]}.check.log"
        grep -q '^FAILED' "out/${presets[$i]}.check.log" && failures=$((failures + 1))
    done
    # Two trees at once: the browser's page modules drive the web client
    # against the native dedicated server over WebTransport (D173).
    if [ "$failures" -eq 0 ]; then
        step "web page"
        if ! node --no-warnings hosts/web_client/tests/browser_page.mjs \
            out/clang-development/hosts/dedicated_server/rawframe-server \
            out/wasm-development/hosts/web_client/rawframe-web-client.wasm "$PWD" >out/web-page.log 2>&1; then
            tail -30 out/web-page.log; fail "web page"
        else
            grep '^page:' out/web-page.log
        fi
    fi
    # Measured last, alone, so the builds do not share the machine with it.
    if [ "$failures" -eq 0 ]; then
        step "tick budget"
        tools/bench.sh check || fail "tick budget"
    fi
fi

elapsed=$(( $(date +%s) - start ))
if [ "$failures" -eq 0 ]; then
    printf 'check %s passed in %ss\n' "$tier" "$elapsed"
else
    printf 'check %s FAILED (%s) in %ss\n' "$tier" "$failures" "$elapsed"
    exit 1
fi
