#!/usr/bin/env bash
# Install MediaMTX as a systemd service that starts at boot (rover Jetson).
#
#   sudo ./install.sh              install or update, then (re)start
#   sudo ./install.sh --uninstall  stop it and remove everything this installed
#
# Rerun after changing mediamtx.yml or VERSION below.
set -euo pipefail

VERSION=v1.21.4
# From the release's checksums.sha256. Update together with VERSION.
declare -A SHA256=(
  [amd64]=be0c05d9c8af8338116977e7f1c3fcb409fbf882075e5ab075cf8baad76a59d2
  [arm64]=47784081da54c0608ea4dc359f5cfb19880209434769c532b3ed3a67ea99c29d
)

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN=/usr/local/bin/mediamtx
CONF_DIR=/etc/mediamtx
UNIT=/etc/systemd/system/mediamtx.service

[[ $EUID -eq 0 ]] || { echo "Run with sudo: sudo $0 $*" >&2; exit 1; }

if [[ "${1:-}" == "--uninstall" ]]; then
  systemctl disable --now mediamtx 2>/dev/null || true
  rm -f "$UNIT" "$BIN"
  rm -rf "$CONF_DIR"
  systemctl daemon-reload
  echo "MediaMTX service removed"
  exit 0
fi

case "$(uname -m)" in
  x86_64)  ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  *) echo "Unsupported CPU: $(uname -m)" >&2; exit 1 ;;
esac

if [[ -x "$BIN" ]] && [[ "$("$BIN" --version 2>/dev/null)" == "$VERSION" ]]; then
  echo "MediaMTX $VERSION already installed"
else
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  TARBALL="mediamtx_${VERSION}_linux_${ARCH}.tar.gz"
  echo "Downloading $TARBALL"
  curl -fsSL -o "$TMP/$TARBALL" \
    "https://github.com/bluenviron/mediamtx/releases/download/${VERSION}/${TARBALL}"
  echo "${SHA256[$ARCH]}  $TMP/$TARBALL" | sha256sum --check --quiet
  tar -xzf "$TMP/$TARBALL" -C "$TMP" mediamtx
  install -m 755 "$TMP/mediamtx" "$BIN"
  echo "Installed MediaMTX $VERSION to $BIN"
fi

install -D -m 644 "$HERE/mediamtx.yml" "$CONF_DIR/mediamtx.yml"
install -m 644 "$HERE/mediamtx.service" "$UNIT"
systemctl daemon-reload
systemctl enable mediamtx
systemctl restart mediamtx

sleep 1
if systemctl is-active --quiet mediamtx; then
  echo "MediaMTX is running and starts at boot. Logs: journalctl -u mediamtx -f"
else
  echo "MediaMTX failed to start:" >&2
  journalctl -u mediamtx -n 20 --no-pager >&2
  exit 1
fi
