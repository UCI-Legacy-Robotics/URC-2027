#!/usr/bin/env bash
# Start the rover side on the Jetson: MediaMTX + the ZED publisher.
# Any options are passed through to publish-zed.sh, e.g.
#   ./start-rover.sh --bitrate 1000
#   ./start-rover.sh --test          # test pattern, no camera
# Logs go to ../logs/. Ctrl+C stops both.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
LOGS="$ROOT/logs"
MTX="$ROOT/bin/mediamtx"
mkdir -p "$LOGS"

[[ -x "$MTX" ]] || { echo "MediaMTX not found; run ./setup-jetson.sh first" >&2; exit 1; }

killtree() { local c; for c in $(pgrep -P "$1"); do killtree "$c"; done; kill "$1" 2>/dev/null || true; }
cleanup() { trap - EXIT INT TERM; for c in $(pgrep -P $$); do killtree "$c"; done; wait 2>/dev/null || true; }
trap cleanup EXIT INT TERM

"$MTX" "$HERE/mediamtx-rover.yml" > >(tee "$LOGS/mediamtx-rover.log" | sed 's/^/[mtx] /') 2>&1 &

# Wait for the RTSP port before publishing.
for _ in $(seq 50); do (exec 3<>/dev/tcp/127.0.0.1/8554) 2>/dev/null && break; sleep 0.1; done

# Restart the publisher if it exits (camera unplugged, MediaMTX restarted, ...).
(
  while true; do
    "$HERE/publish-zed.sh" "$@" || true
    echo "publisher exited; restarting in 2 s"
    sleep 2
  done
) > >(tee "$LOGS/publish-zed.log" | sed 's/^/[pub] /') 2>&1 &

ips="$(hostname -I 2>/dev/null | tr ' ' '\n' | grep -v '^$' | grep -v ':' | tr '\n' ' ')"
echo
echo "Rover is up. On the base station run:  ./base/start-base.sh <one of: ${ips:-rover IP}>"
echo "Direct-from-rover viewer (for latency comparison): http://<rover IP>:8889/rover/front"
echo
wait
