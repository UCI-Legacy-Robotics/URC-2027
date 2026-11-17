#!/usr/bin/env bash
# Download the pinned MediaMTX release for this machine's CPU into bin/.
# Works on both the Jetson (arm64) and the base station laptop (amd64).
set -euo pipefail

VERSION="${MEDIAMTX_VERSION:-v1.21.4}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/bin"

case "$(uname -m)" in
  x86_64)  ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  *) echo "unsupported CPU: $(uname -m)" >&2; exit 1 ;;
esac

if [[ -x "$BIN/mediamtx" ]] && "$BIN/mediamtx" --help 2>&1 | grep -q "${VERSION}"; then
  echo "MediaMTX ${VERSION} already in $BIN"; exit 0
fi

URL="https://github.com/bluenviron/mediamtx/releases/download/${VERSION}/mediamtx_${VERSION}_linux_${ARCH}.tar.gz"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $URL"
curl -fsSL -o "$TMP/mediamtx.tar.gz" "$URL"
tar -xzf "$TMP/mediamtx.tar.gz" -C "$TMP" mediamtx
mkdir -p "$BIN"
install -m 755 "$TMP/mediamtx" "$BIN/mediamtx"
echo "Installed MediaMTX ${VERSION} (${ARCH}) to $BIN/mediamtx"
