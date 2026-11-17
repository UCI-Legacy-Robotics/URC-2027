#!/usr/bin/env bash
# Run rover and base on ONE machine (base pulls from 127.0.0.1), to check the
# setup before involving the radio. Options are passed to publish-zed.sh;
# use --test if no ZED is plugged in. Ctrl+C stops everything.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
[[ -x "$HERE/bin/mediamtx" ]] || "$HERE/install-mediamtx.sh"

killtree() { local c; for c in $(pgrep -P "$1"); do killtree "$c"; done; kill "$1" 2>/dev/null || true; }
cleanup() { trap - EXIT INT TERM; for c in $(pgrep -P $$); do killtree "$c"; done; wait 2>/dev/null || true; }
trap cleanup EXIT INT TERM

"$HERE/rover/start-rover.sh" "$@" &
"$HERE/base/start-base.sh" 127.0.0.1 2>&1 | sed 's/^/[base] /' &
wait
