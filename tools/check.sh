#!/usr/bin/env bash
# The one definition of "it passes".
#
#   tools/check.sh fast   repository rules, format, one incremental build, tests
#   tools/check.sh        the above plus GCC and Clang in every configuration,
#                         the address and thread sanitizers, the web build
#                         (wasm32 without threads, tests under Node), the web
#                         page against the dedicated server over WebTransport,
#                         a real browser playing from a canvas where Puppeteer
#                         is installed,
#                         and the tick budget (tools/bench.sh check) and the
#                         web client's download and start budget
#                         (tools/web_budget.sh check) once all pass
#
# Build trees live under out/ and are reused, so a second run only rebuilds
# what changed.
set -euo pipefail
cd "$(dirname "$0")/.."
tier="${1:-full}"
start=$(date +%s)
failures=0

# Each step says how long the one before it took, so a check that grows
# says where.
last=$(date +%s)
step() {
    local now
    now=$(date +%s)
    printf '== %s (previous step %ss)\n' "$*" "$((now - last))"
    last=$now
}
fail() { printf 'FAILED: %s\n' "$*"; failures=$((failures + 1)); }

mkdir -p out

# Where Mesa's software Vulkan (lavapipe) is installed, as on the machine
# the CI runs on, a device test that finds no adapter fails instead of
# skipping (D277): pixels are checked, not assumed. The loader sees lavapipe
# alone, so every run draws on the same rasterizer, and no other driver
# probes this machine's hardware from inside a sanitized test (radv did,
# and leaked).
lavapipe=$(compgen -G "/usr/share/vulkan/icd.d/lvp_icd*.json" | head -1 || true)
if [ -n "$lavapipe" ]; then
    export RAWFRAME_REQUIRE_GPU=1 VK_DRIVER_FILES="$lavapipe"
fi
# Where the shader toolchain is installed, as on the machine the CI runs on,
# the pinned Slang compiler where tools/gen_shaders.py looks for it and the
# cross compilers on the path, a material that needs its own program is
# cooked through it, and a test that finds no toolchain fails instead of
# skipping (D484).
slangc="${RAWFRAME_SLANGC:-$HOME/.cache/rawframe/slang-2026.19/bin/slangc}"
dxc_bin=$(compgen -G "/opt/dxc/*/bin" | head -1 || true)
if [ -x "$slangc" ] && [ -n "$dxc_bin" ] && command -v spirv-cross >/dev/null; then
    export RAWFRAME_REQUIRE_SHADER_TOOLCHAIN=1 RAWFRAME_SLANGC="$slangc" PATH="$dxc_bin:$PATH"
fi

step "repository rules"
python3 tools/check_repo.py || fail "repository rules"

step "format"
# Files not yet added count too, so a check before a commit sees them.
mapfile -t sources < <(git ls-files --cached --others --exclude-standard '*.h' '*.cpp' | grep -v '^third_party/' || true)
if [ "${#sources[@]}" -gt 0 ]; then
    if ! clang-format-20 --dry-run --Werror "${sources[@]}" >out/format.log 2>&1; then
        head -20 out/format.log; fail "format (run: clang-format-20 -i on the files above)"
    fi
fi

# Test jobs per preset: the whole machine for one preset, a quarter of it
# each when eight run at once, so tests that play in real time are not
# starved by eight times as many jobs as cores.
jobs=$(nproc)

build_and_test() {
    local preset="$1"
    step "$preset"
    # A tree whose cache no longer fits its preset (a compiler moved, say)
    # is made again from nothing, once.
    if ! cmake --preset "$preset" >"out/$preset.configure.log" 2>&1 &&
        ! { rm -rf "out/$preset" && cmake --preset "$preset" >"out/$preset.configure.log" 2>&1; }; then
        tail -20 "out/$preset.configure.log"; fail "$preset configure"; return
    fi
    local began built
    began=$(date +%s)
    if ! cmake --build "out/$preset" >"out/$preset.build.log" 2>&1; then
        grep -E 'error|FAILED' "out/$preset.build.log" | head -20; fail "$preset build"; return
    fi
    built=$(date +%s)
    # Window tests play in real time against a software rasterizer, and gcc's
    # optimized tree finds nothing in them that clang's optimized tree, the
    # sanitizers, and gcc's slow debug tree do not; it leaves them out (D493).
    # So does clang-development in the full check: the fast check before
    # every push plays them there, alone, and gcc's debug tree plays them
    # under the full check's load (D500).
    local leave=()
    if [ "$preset" = gcc-shipping ] || { [ "$preset" = clang-development ] && [ "$tier" = full ]; }; then
        leave=(-LE window)
    fi
    if ! ctest --test-dir "out/$preset" "${leave[@]}" --output-on-failure -j "$jobs" >"out/$preset.test.log" 2>&1; then
        # Each failed test's status and its output's last lines, cut short
        # (its records are long), so a failure seen only under CI's load can
        # be read there.
        awk 'function flush() { if (shown) { print head; for (i = (n > 40 ? n - 39 : 1); i <= n; i++) print kept[i] } }
             /Test +#[0-9]+: / { flush(); shown = /\*\*\*|Failed|Exception|Timeout/; head = $0; n = 0; next }
             /^(The following tests|[0-9]+% tests passed)/ { flush(); shown = 0 }
             shown { kept[++n] = substr($0, 1, 1200) }
             END { flush() }' "out/$preset.test.log"
        tail -30 "out/$preset.test.log"; fail "$preset tests"; return
    fi
    printf '   %s: built in %ss, tested in %ss\n' "$preset" "$((built - began))" "$(($(date +%s) - built))"
}

if [ "$tier" = "fast" ]; then
    build_and_test clang-development
else
    # Independent build trees, so they build in parallel.
    jobs=$(( ($(nproc) + 3) / 4 ))
    presets=(gcc-debug gcc-shipping clang-development clang-shipping clang-sanitize clang-thread wasm-development wasm-shipping)
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
        if ! tools/node_page.sh hosts/web_client/tests/browser_page.mjs \
            out/clang-development/hosts/dedicated_server/rawframe-server \
            out/wasm-development/hosts/web_client/rawframe-web-client.wasm "$PWD" >out/web-page.log 2>&1; then
            tail -30 out/web-page.log; fail "web page"
        else
            grep '^page:' out/web-page.log
        fi
        # A real browser plays from a canvas (D250), where Puppeteer and its
        # browser are installed: RAWFRAME_NODE_MODULES and
        # PUPPETEER_CACHE_DIR, or under /opt/webtest, which any user
        # (the CI runner's too) can read.
        # Runners, then the plaza, the sample 3D game (D287), each exported
        # for the web and played from its site (D397).
        for game in runners plaza; do
            step "web play $game"
            play_status=0
            RAWFRAME_NODE_MODULES="${RAWFRAME_NODE_MODULES:-/opt/webtest/node_modules}" \
                PUPPETEER_CACHE_DIR="${PUPPETEER_CACHE_DIR:-/opt/webtest/cache}" \
                tools/node_page.sh hosts/web_client/tests/browser_play.mjs \
                out/clang-development/hosts/export/rawframe-export \
                out/clang-development/hosts/cook/rawframe-cook out/clang-development/hosts/build/rawframe-build \
                out/clang-development/hosts/dedicated_server/rawframe-server \
                out/clang-development/hosts/bots/rawframe-bots \
                out/wasm-development/hosts/web_client/rawframe-web-client.wasm \
                out/wasm-development/third_party/maul-window/maul-window.mjs \
                out/wasm-development/third_party/maul-rhi/maul-rhi.mjs "$PWD" \
                "$game" >"out/web-play-$game.log" 2>&1 ||
                play_status=$?
            if [ "$play_status" -eq 77 ]; then
                echo "web play skipped: no Puppeteer or no browser for it"
            elif [ "$play_status" -ne 0 ]; then
                # The page's own lines say which part failed; the tail is
                # the client's summaries after them.
                grep '^page:' "out/web-play-$game.log"
                tail -30 "out/web-play-$game.log"; fail "web play $game"
            else
                grep '^page:' "out/web-play-$game.log"
            fi
        done
    fi
    # Every engine shader's WGSL compiled by the browser's WebGPU (D286).
    if [ "$failures" -eq 0 ]; then
        step "web shaders"
        wgsl_status=0
        RAWFRAME_NODE_MODULES="${RAWFRAME_NODE_MODULES:-/opt/webtest/node_modules}" \
            PUPPETEER_CACHE_DIR="${PUPPETEER_CACHE_DIR:-/opt/webtest/cache}" \
            tools/node_page.sh tools/check_wgsl.mjs "$PWD" >out/web-shaders.log 2>&1 || wgsl_status=$?
        if [ "$wgsl_status" -eq 77 ]; then
            echo "web shaders skipped: no Puppeteer or no browser for it"
        elif [ "$wgsl_status" -ne 0 ]; then
            tail -30 out/web-shaders.log; fail "web shaders"
        else
            grep '^page: [0-9]' out/web-shaders.log
        fi
    fi
    # Measured last, alone, so the builds do not share the machine with it.
    if [ "$failures" -eq 0 ]; then
        step "tick budget"
        tools/bench.sh check || fail "tick budget"
        step "web budget"
        tools/web_budget.sh check || fail "web budget"
    fi
fi

printf '   (last step %ss)\n' "$(($(date +%s) - last))"
elapsed=$(( $(date +%s) - start ))
if [ "$failures" -eq 0 ]; then
    printf 'check %s passed in %ss\n' "$tier" "$elapsed"
else
    printf 'check %s FAILED (%s) in %ss\n' "$tier" "$failures" "$elapsed"
    exit 1
fi
