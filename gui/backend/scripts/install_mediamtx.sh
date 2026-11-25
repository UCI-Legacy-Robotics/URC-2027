#!/usr/bin/env bash
# Download the pinned MediaMTX to gui/backend/bin/ for the backend to run.
# Needs internet: run it before going to the field.
set -euo pipefail

# Same release as the rover (rover_bringup/mediamtx/install.sh); keep them in step.
VERSION=v1.21.4
declare -A SHA256=(
  [amd64]=be0c05d9c8af8338116977e7f1c3fcb409fbf882075e5ab075cf8baad76a59d2
  [arm64]=47784081da54c0608ea4dc359f5cfb19880209434769c532b3ed3a67ea99c29d
)

BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/mediamtx"

case "$(uname -m)" in
  x86_64)  ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  *) echo "Unsupported CPU: $(uname -m)" >&2; exit 1 ;;
esac

if [[ -x "$BIN" ]] && [[ "$("$BIN" --version 2>/dev/null)" == "$VERSION" ]]; then
  echo "MediaMTX $VERSION already at $BIN"
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
TARBALL="mediamtx_${VERSION}_linux_${ARCH}.tar.gz"
echo "Downloading $TARBALL"
curl -fsSL -o "$TMP/$TARBALL" \
  "https://github.com/bluenviron/mediamtx/releases/download/${VERSION}/${TARBALL}"
echo "${SHA256[$ARCH]}  $TMP/$TARBALL" | sha256sum --check --quiet
tar -xzf "$TMP/$TARBALL" -C "$TMP" mediamtx
install -D -m 755 "$TMP/mediamtx" "$BIN"
echo "Installed MediaMTX $VERSION to $BIN"
