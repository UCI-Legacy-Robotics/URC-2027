#!/usr/bin/env bash
# Show each MediaMTX path: whether it has a live stream, its readers, and bytes
# received. On the base, "ready" = currently pulling from the rover.
# Usage: ./paths-status.sh [api port]   (default 19997 = base; 9997 = rover)
#        watch -n1 ./paths-status.sh    to watch on-demand pulls start and stop
set -euo pipefail
PORT="${1:-19997}"
curl -fsS "http://127.0.0.1:${PORT}/v3/paths/list" | python3 -c '
import json, sys
row = "{:<18} {:<6} {:<40} {:>8}  {}"
print(row.format("path", "ready", "readers", "rx MB", "source"))
for p in json.load(sys.stdin)["items"]:
    src = (p.get("source") or {}).get("type", "-")
    readers = ",".join(r["type"] for r in p["readers"]) or "-"
    print(row.format(p["name"], str(p["ready"]).lower(), readers,
                     "%.2f" % (p.get("bytesReceived", 0) / 1e6), src))
'
