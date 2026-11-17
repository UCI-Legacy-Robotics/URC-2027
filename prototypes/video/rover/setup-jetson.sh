#!/usr/bin/env bash
# One-time setup on the Jetson Orin Nano: GStreamer plugins, MediaMTX, and a
# report of what the pipeline needs. Safe to re-run.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "== Installing GStreamer packages (needs sudo) =="
# plugins-good: v4l2src, videocrop, rtp   plugins-ugly: x264enc
# plugins-bad: h264parse                  gstreamer1.0-rtsp: rtspclientsink
sudo apt-get update
sudo apt-get install -y \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-rtsp \
  v4l-utils curl

echo
echo "== MediaMTX =="
"$HERE/../install-mediamtx.sh"

echo
echo "== Versions (copy these into the README) =="
cat /etc/nv_tegra_release 2>/dev/null || echo "not a Jetson? /etc/nv_tegra_release missing"
dpkg-query -W -f='${Package} ${Version}\n' nvidia-jetpack 2>/dev/null || true
gst-launch-1.0 --version | head -1
dpkg-query -W -f='x264: ${Version}\n' 'libx264-*' 2>/dev/null | head -1 || true
"$HERE/../bin/mediamtx" --help 2>&1 | grep -m1 -o 'v[0-9.]*' | sed 's/^/MediaMTX /'
sudo nvpmodel -q 2>/dev/null | head -2 || true

echo
echo "== Required GStreamer elements =="
for e in v4l2src videocrop videoconvert x264enc h264parse rtspclientsink nvvidconv nvv4l2h264enc; do
  if gst-inspect-1.0 "$e" >/dev/null 2>&1; then echo "  ok      $e"; else echo "  MISSING $e"; fi
done
echo "  (nvv4l2h264enc may be listed but can't encode on an Orin Nano: it has no NVENC)"

echo
echo "== Cameras =="
v4l2-ctl --list-devices 2>/dev/null || true
for d in /sys/class/video4linux/video*; do
  if grep -qi zed "$d/name" 2>/dev/null && [[ "$(cat "$d/index")" == 0 ]]; then
    echo "ZED at /dev/$(basename "$d"). Formats:"
    v4l2-ctl -d "/dev/$(basename "$d")" --list-formats-ext
  fi
done
