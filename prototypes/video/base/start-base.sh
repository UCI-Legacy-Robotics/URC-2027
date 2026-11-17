#!/usr/bin/env bash
# Start the base station MediaMTX, pulling from the rover at ROVER_HOST.
# Usage: ./start-base.sh <rover IP or hostname>
# Logs go to ../logs/. Ctrl+C stops it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
LOGS="$ROOT/logs"
MTX="$ROOT/bin/mediamtx"

ROVER_HOST="${1:-${ROVER_HOST:-}}"
[[ -n "$ROVER_HOST" ]] || { echo "usage: $0 <rover IP or hostname>" >&2; exit 1; }
[[ -x "$MTX" ]] || "$ROOT/install-mediamtx.sh"
mkdir -p "$LOGS"

CONF="$LOGS/mediamtx-base.generated.yml"
sed "s/ROVER_HOST/${ROVER_HOST}/g" "$HERE/mediamtx-base.yml" > "$CONF"

if (exec 3<>"/dev/tcp/${ROVER_HOST}/8554") 2>/dev/null; then
  echo "Rover RTSP port ${ROVER_HOST}:8554 is reachable."
else
  echo "WARNING: can't reach ${ROVER_HOST}:8554. Is start-rover.sh running and the radio linked?"
fi

echo
echo "Open in a browser (each pulls from the rover only while open):"
echo "  RTSP/TCP  http://localhost:18889/rover/front"
echo "  RTSP/UDP  http://localhost:18889/rover/front-udp"
echo "  SRT       http://localhost:18889/rover/front-srt"
echo "Direct from rover (comparison): http://${ROVER_HOST}:8889/rover/front"
echo "Check who is pulling:  ./base/paths-status.sh"
echo

exec "$MTX" "$CONF" > >(tee "$LOGS/mediamtx-base.log") 2>&1
