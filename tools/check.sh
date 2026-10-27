#!/usr/bin/env bash
# The checks, in four tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            repository rules, the documentation check,
#                            formatting, clang-development built, and the
#                            tests the change reaches but the integration
#                            ones. Run it before every push.
#   tools/check.sh pr        what the pull request check runs: every rule on
#                            every file, clang-development and clang-sanitize
#                            built, the reached tests in both trees, and the
#                            integration tests the change reaches in
#                            clang-development, a few at a time.
#   tools/check.sh full      what main runs after a merge: every test in
#                            clang-sanitize, gcc-debug and clang-thread, the
#                            web build and its tests, the web page against the
#                            dedicated server, real browsers playing the sample
#                            games, and every integration test.
#   tools/check.sh nightly   also clang-shipping and wasm-shipping, and the
#                            tick and web download budgets measured in them.
#
# The builds are the presets of CMakePresets.json, in out/<preset>, reused
# from run to run. Each tree's logs sit beside it. JOBS (default 8) bounds
# the jobs of the whole check, split between the trees that build at once.
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full | nightly) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full|nightly]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
mkdir -p out
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}
start=$(date +%s)
failures=0

# Tests that drive windows, browsers, headsets or played games: a few
# end-to-end scenarios that may read the clock, kept out of the changed tier
# and run a few at a time, since each plays a server, a client and a GPU on
# the processor.
integration='^integration$'

step() { printf '\n== %s (%ss so far)\n' "$*" "$(($(date +%s) - start))"; }
fail() {
    printf 'FAILED: %s\n' "$*"
    failures=$((failures + 1))
}

# Where Mesa's software Vulkan (lavapipe) is installed, a device test that
# finds no adapter fails instead of skipping: pixels are checked, not
# assumed. The loader sees lavapipe alone, so every run draws on the same
# rasterizer and no hardware driver is probed from a sanitized test.
lavapipe=$(compgen -G "/usr/share/vulkan/icd.d/lvp_icd*.json" | head -1 || true)
if [ -n "$lavapipe" ]; then
    export RAWFRAME_REQUIRE_GPU=1 VK_DRIVER_FILES="$lavapipe"
fi
# Where the shader toolchain is installed (the pinned Slang compiler, DXC and
# SPIRV-Cross), a material that needs its own program is cooked through it,
# and a test that finds no toolchain fails instead of skipping.
slang_release=2026.19
slangc="${RAWFRAME_SLANGC:-$HOME/.cache/rawframe/slang-$slang_release/bin/slangc}"
dxc_bin=$(compgen -G "/opt/dxc/*/bin" | head -1 || true)
if [ -x "$slangc" ] && [ -n "$dxc_bin" ] && command -v spirv-cross >/dev/null; then
    export RAWFRAME_REQUIRE_SHADER_TOOLCHAIN=1 RAWFRAME_SLANGC="$slangc" PATH="$dxc_bin:$PATH"
fi

rules() {
    step "repository rules"
    python3 tools/check_repo.py || fail "repository rules"
    if [ -f "$org/scripts/docs_check.py" ]; then
        local exclude=()
        while read -r path; do
            [ -n "$path" ] && exclude+=(--exclude "$path")
        done < <(grep -v '^#' tools/docs-check-exclude.txt)
        python3 "$org/scripts/docs_check.py" "${exclude[@]}" || fail "documentation check"
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
}

format() {
    step "format"
    local sources
    if [ "$tier" = changed ]; then
        local base
        base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
        mapfile -t sources < <({
            git diff --name-only --diff-filter=d "$base" -- '*.h' '*.cpp'
            git ls-files --others --exclude-standard '*.h' '*.cpp'
        } | grep -v '^third_party/' | sort -u || true)
        if [ "${#sources[@]}" -eq 0 ]; then
            echo "no changed C++ file"
            return
        fi
    else
        mapfile -t sources < <(git ls-files --cached --others --exclude-standard '*.h' '*.cpp' |
            grep -v '^third_party/' || true)
        if [ "${#sources[@]}" -eq 0 ]; then
            fail "format: no C++ file found"
            return
        fi
    fi
    if ! clang-format-20 --dry-run --Werror "${sources[@]}" >out/format.log 2>&1; then
        head -20 out/format.log
        fail "format (run clang-format-20 -i on the files above)"
    fi
}

# Builds one preset and runs its tests. `select` is reached (the tests the
# change can reach), all, or none; `labels` is unit (every test but the
# integration ones), integration (those alone), or every.
build_and_test() {
    local preset=$1 select=$2 labels=$3 run_jobs=$4
    step "$preset"
    if ! cmake --preset "$preset" >"out/$preset.configure.log" 2>&1; then
        # A tree whose cache no longer fits its preset (a compiler moved) is
        # made again from nothing, once.
        rm -rf "out/$preset"
        if ! cmake --preset "$preset" >"out/$preset.configure.log" 2>&1; then
            tail -20 "out/$preset.configure.log"
            fail "$preset configure"
            return
        fi
    fi
    local began built
    began=$(date +%s)
    if ! cmake --build "out/$preset" -j "$run_jobs" >"out/$preset.build.log" 2>&1; then
        grep -E 'error|FAILED' "out/$preset.build.log" | head -20
        fail "$preset build"
        return
    fi
    built=$(date +%s)
    [ "$select" = none ] && {
        printf '   %s: built in %ss\n' "$preset" "$((built - began))"
        return
    }
    local filter=()
    case "$labels" in
    unit) filter=(-LE "$integration") ;;
    integration) filter=(-L "$integration") ;;
    esac
    if [ "$select" = reached ]; then
        local reached
        mapfile -t reached < <(python3 tools/affected.py "out/$preset")
        if [ "${#reached[@]}" -eq 0 ]; then
            printf '   %s: built in %ss, no test reached\n' "$preset" "$((built - began))"
            return
        fi
        filter+=(-R "^($(IFS='|'; echo "${reached[*]}"))\$")
    fi
    # Integration tests play a server, a client and a GPU each: a few at once.
    local test_jobs=$run_jobs
    [ "$labels" = integration ] && test_jobs=$((run_jobs < 4 ? run_jobs : 4))
    # A test that sets no limit of its own ends after twenty minutes, so one
    # that hangs ends the check rather than holding it.
    if ctest --test-dir "out/$preset" "${filter[@]}" --output-on-failure --timeout 1200 \
        -j "$test_jobs" >"out/$preset.$labels.log" 2>&1; then
        printf '   %s: built in %ss, %s tests in %ss: %s\n' "$preset" "$((built - began))" "$labels" \
            "$(($(date +%s) - built))" "$(grep -E '^[0-9]+% tests passed' "out/$preset.$labels.log" || echo none)"
    elif grep -q 'No tests were found' "out/$preset.$labels.log"; then
        printf '   %s: built in %ss, no %s test selected\n' "$preset" "$((built - began))" "$labels"
    else
        # Each failed test's last lines, cut short, then the summary. CI's
        # logs are public, and libFuzzer prints a finding's input from its
        # first mutation line on, so a fuzz target's output stops there: the
        # finding is reproduced by running the target locally.
        awk 'function flush() { if (shown) { print head; for (i = (n > 40 ? n - 39 : 1); i <= n; i++) print kept[i] } }
             /Test +#[0-9]+: / { flush(); shown = /\*\*\*|Failed|Exception|Timeout/; head = $0; n = 0
                                 fuzz = /_fuzz_/; dump = 0; next }
             /^(The following tests|[0-9]+% tests passed)/ { flush(); shown = 0 }
             fuzz && /^MS: [0-9]/ { dump = 1 }
             shown && !dump { kept[++n] = substr($0, 1, 1200) }
             END { flush() }' "out/$preset.$labels.log"
        sed -n '/^[0-9]*% tests passed/,$p' "out/$preset.$labels.log"
        fail "$preset $labels tests"
    fi
}

# Runs build_and_test for each "preset select labels" triple at once, the
# jobs shared between them, and prints each tree's report in order. What
# does not divide evenly goes to the first triples, one job each, so the
# slowest trees are named first; each tree has at least one job.
in_parallel() {
    local base=$((jobs / $#)) extra=$((jobs % $#))
    local pids=() names=() spec share
    for spec in "$@"; do
        local name=${spec// /.}
        share=$((base + (extra > 0 ? 1 : 0)))
        [ "$share" -ge 1 ] || share=1
        extra=$((extra > 0 ? extra - 1 : 0))
        # shellcheck disable=SC2086
        (build_and_test $spec "$share") >"out/$name.check.log" 2>&1 &
        pids+=($!)
        names+=("$name")
    done
    local i
    for i in "${!pids[@]}"; do
        wait "${pids[$i]}" || true
        cat "out/${names[$i]}.check.log"
        if grep -q '^FAILED' "out/${names[$i]}.check.log"; then
            failures=$((failures + 1))
        fi
    done
}

# The web page against the native dedicated server over WebTransport, and
# every engine shader's WGSL compiled by the browser's WebGPU.
web_steps() {
    step "web page"
    if tools/node_page.sh hosts/web_client/tests/browser_page.mjs \
        out/clang-development/hosts/dedicated_server/rawframe-server \
        out/wasm-development/hosts/web_client/rawframe-web-client.wasm "$root" >out/web-page.log 2>&1; then
        grep '^page:' out/web-page.log
    else
        tail -30 out/web-page.log
        fail "web page"
    fi
    step "web shaders"
    local status=0
    RAWFRAME_NODE_MODULES="${RAWFRAME_NODE_MODULES:-/opt/webtest/node_modules}" \
        PUPPETEER_CACHE_DIR="${PUPPETEER_CACHE_DIR:-/opt/webtest/cache}" \
        tools/node_page.sh tools/check_wgsl.mjs "$root" >out/web-shaders.log 2>&1 || status=$?
    if [ "$status" -eq 77 ]; then
        echo "web shaders skipped: no Puppeteer or no browser for it"
    elif [ "$status" -ne 0 ]; then
        tail -30 out/web-shaders.log
        fail "web shaders"
    else
        grep '^page: [0-9]' out/web-shaders.log
    fi
}

# A real browser plays a 2D game and a 3D one from a canvas against a
# dedicated server, after the trees, never beside them: a page drawing a few
# frames a second misses its own expectations.
web_plays() {
    local game status
    for game in runners plaza; do
        step "web play $game"
        status=0
        RAWFRAME_NODE_MODULES="${RAWFRAME_NODE_MODULES:-/opt/webtest/node_modules}" \
            PUPPETEER_CACHE_DIR="${PUPPETEER_CACHE_DIR:-/opt/webtest/cache}" \
            tools/node_page.sh hosts/web_client/tests/browser_play.mjs \
            out/clang-development/hosts/export/rawframe-export \
            out/clang-development/hosts/cook/rawframe-cook out/clang-development/hosts/build/rawframe-build \
            out/clang-development/hosts/dedicated_server/rawframe-server \
            out/clang-development/hosts/bots/rawframe-bots \
            out/wasm-development/hosts/web_client/rawframe-web-client.wasm \
            out/wasm-development/third_party/maul-window/maul-window.mjs \
            out/wasm-development/third_party/maul-rhi/maul-rhi.mjs \
            out/wasm-development/third_party/maul-ui/maul-ui.mjs "$root" \
            "$game" >"out/web-play-$game.log" 2>&1 || status=$?
        if [ "$status" -eq 77 ]; then
            echo "web play skipped: no Puppeteer or no browser for it"
        elif [ "$status" -ne 0 ]; then
            grep '^page:' "out/web-play-$game.log" || true
            tail -30 "out/web-play-$game.log"
            fail "web play $game"
        else
            grep '^page:' "out/web-play-$game.log"
        fi
    done
}

rules
format

case "$tier" in
changed)
    build_and_test clang-development reached unit "$jobs"
    ;;
pr)
    in_parallel "clang-development reached unit" "clang-sanitize reached unit"
    [ "$failures" -eq 0 ] && build_and_test clang-development reached integration "$jobs"
    ;;
full | nightly)
    # Slowest first, as in_parallel hands out the jobs.
    in_parallel "clang-thread all unit" "clang-sanitize all unit" "gcc-debug all unit" \
        "clang-development all unit" "wasm-development all every"
    if [ "$failures" -eq 0 ]; then
        web_steps
        web_plays
        build_and_test clang-development all integration "$jobs"
    fi
    if [ "$tier" = nightly ] && [ "$failures" -eq 0 ]; then
        in_parallel "clang-shipping none every" "wasm-shipping none every"
        if [ "$failures" -eq 0 ]; then
            step "tick budget"
            tools/bench.sh check || fail "tick budget"
            step "web budget"
            tools/web_budget.sh check || fail "web budget"
        fi
    fi
    ;;
esac

elapsed=$(($(date +%s) - start))
if [ "$failures" -eq 0 ]; then
    printf '\ncheck %s passed in %ss\n' "$tier" "$elapsed"
else
    printf '\ncheck %s FAILED (%s) in %ss\n' "$tier" "$failures" "$elapsed"
    exit 1
fi
