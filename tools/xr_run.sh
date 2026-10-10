#!/usr/bin/env bash
# Runs a command against an OpenXR runtime of its own (D591): Monado's
# service, headless (its null compositor, which shows nothing, its
# simulated headset, and a simulated simple controller in each hand, D596),
# in a runtime directory of its own, so tests started at the same moment
# never share a service; and RAWFRAME_REQUIRE_XR set, so a test that finds
# no runtime fails instead of skipping. The service reads
# its standard input to quit, so it is given a pipe held open until the
# command ends. Vulkan is the caller's: the check's lavapipe
# (VK_DRIVER_FILES).
#
# usage: xr_run.sh <command> [<argument>...]
set -uo pipefail

manifest=""
for each in /usr/share/openxr/1/openxr_monado.json /usr/local/share/openxr/1/openxr_monado.json; do
    [ -f "$each" ] && manifest="$each" && break
done
if [ -z "$manifest" ] || ! command -v monado-service >/dev/null; then
    echo "xr_run.sh: Monado is not installed" >&2
    exit 1
fi
place="$(mktemp -d)"
service=""
trap 'exec 4>&-; [ -z "$service" ] || { kill "$service" 2>/dev/null; wait "$service" 2>/dev/null; }; rm -rf "$place"' EXIT
chmod 700 "$place"
mkfifo "$place/input"
export XDG_RUNTIME_DIR="$place" XR_RUNTIME_JSON="$manifest" XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 \
    SIMULATED_LEFT=simple SIMULATED_RIGHT=simple
monado-service <"$place/input" >"$place/service.log" 2>&1 &
service=$!
exec 4>"$place/input"
for _ in $(seq 1 400); do
    [ -S "$place/monado_comp_ipc" ] && break
    if ! kill -0 "$service" 2>/dev/null; then
        echo "xr_run.sh: Monado's service did not start" >&2
        cat "$place/service.log" >&2
        exit 1
    fi
    sleep 0.05
done
RAWFRAME_REQUIRE_XR=1 "$@"
status=$?
[ "$status" -eq 0 ] || { echo "== Monado's service" >&2; tail -20 "$place/service.log" >&2; }
exit "$status"
