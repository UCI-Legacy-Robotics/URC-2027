#!/usr/bin/env bash
# Regenerate app/generated/rover.py from mavlink/rover.xml (MAVLink 2).
set -euo pipefail
cd "$(dirname "$0")/.."

uv run python -m pymavlink.tools.mavgen \
  --lang=Python3 --wire-protocol=2.0 \
  --output=app/generated/rover.py \
  ../../mavlink/rover.xml
