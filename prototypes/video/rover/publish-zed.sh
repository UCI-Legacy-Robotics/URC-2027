#!/usr/bin/env bash
# Encode the ZED 2's left lens to H.264 and publish it to the rover MediaMTX.
#
# The ZED 2 is used as a plain UVC webcam (no ZED SDK): each frame holds both
# lenses side by side, so we crop to the left half before encoding.
#
# The Jetson Orin Nano (including "Super") has NO hardware H.264 encoder, so
# the default encoder is x264 on the CPU. --encoder nvv4l2 is only for Jetsons
# that have NVENC (Orin NX / AGX).
#
# Usage: ./publish-zed.sh [options]
#   --device PATH       camera device (default: auto-detect the ZED by name)
#   --test              use a test pattern instead of the camera
#   --sbs WxH           side-by-side capture size (default 2560x720)
#   --fps N             capture framerate (default 30)
#   --out WxH           encoded size; scales the left half if different (default: half of --sbs)
#   --bitrate KBPS      target bitrate in kbit/s (default 2000)
#   --gop N             keyframe interval in frames (default: = fps, so 1 s)
#   --encoder NAME      x264 (default) | nvv4l2
#   --convert NAME      cpu (default) | vic: crop + convert on the Jetson's VIC
#                       hardware instead of the CPU, leaving more CPU for x264
#   --threads N         x264 threads (default 0 = auto)
#   --url URL           publish target (default rtsp://127.0.0.1:8554/rover/front)
#   --dry-run           print the pipeline and exit
set -euo pipefail

DEVICE=""
TEST=0
SBS="2560x720"
FPS=30
OUT=""
BITRATE=2000
GOP=""
ENCODER="x264"
CONVERT="cpu"
THREADS=0
URL="rtsp://127.0.0.1:8554/rover/front"
DRY_RUN=0

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)  DEVICE="$2"; shift 2 ;;
    --test)    TEST=1; shift ;;
    --sbs)     SBS="$2"; shift 2 ;;
    --fps)     FPS="$2"; shift 2 ;;
    --out)     OUT="$2"; shift 2 ;;
    --bitrate) BITRATE="$2"; shift 2 ;;
    --gop)     GOP="$2"; shift 2 ;;
    --encoder) ENCODER="$2"; shift 2 ;;
    --convert) CONVERT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --url)     URL="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage 0 ;;
    *) echo "unknown option: $1" >&2; usage 1 ;;
  esac
done

SBS_W="${SBS%x*}"; SBS_H="${SBS#*x}"
EYE_W=$((SBS_W / 2))
OUT="${OUT:-${EYE_W}x${SBS_H}}"
OUT_W="${OUT%x*}"; OUT_H="${OUT#*x}"
GOP="${GOP:-$FPS}"

find_zed() {
  local d
  for d in /sys/class/video4linux/video*; do
    [[ -r "$d/name" ]] || continue
    # The ZED registers two nodes; index 0 is the one that streams video.
    if grep -qi 'zed' "$d/name" && [[ "$(cat "$d/index" 2>/dev/null || echo 0)" == 0 ]]; then
      echo "/dev/$(basename "$d")"; return 0
    fi
  done
  return 1
}

# --- source: raw side-by-side frames -----------------------------------------
if [[ $TEST == 1 ]]; then
  SRC="videotestsrc is-live=true pattern=smpte horizontal-speed=4 ! video/x-raw,format=YUY2,width=${SBS_W},height=${SBS_H},framerate=${FPS}/1"
else
  if [[ -z "$DEVICE" ]]; then
    DEVICE="$(find_zed)" || { echo "No ZED camera found. Plug it into a USB 3 port, or pass --device /dev/videoN." >&2; exit 1; }
  fi
  SRC="v4l2src device=${DEVICE} ! video/x-raw,format=YUY2,width=${SBS_W},height=${SBS_H},framerate=${FPS}/1"
fi

# Drop frames instead of queueing them if the encoder falls behind, so
# latency stays flat under load.
LEAKY="queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream"

# --- crop to the left lens + convert ------------------------------------------
case "$CONVERT" in
  cpu)
    CROP="videocrop right=${EYE_W} ! videoconvert ! videoscale ! video/x-raw,format=I420,width=${OUT_W},height=${OUT_H}"
    ;;
  vic)
    # nvvidconv crops with left/right/top/bottom as pixel coordinates of the
    # kept rectangle, not as amounts to trim.
    if [[ "$ENCODER" == "nvv4l2" ]]; then
      CROP="nvvidconv left=0 right=${EYE_W} top=0 bottom=${SBS_H} ! video/x-raw(memory:NVMM),format=I420,width=${OUT_W},height=${OUT_H}"
    else
      CROP="nvvidconv left=0 right=${EYE_W} top=0 bottom=${SBS_H} ! video/x-raw,format=I420,width=${OUT_W},height=${OUT_H}"
    fi
    ;;
  *) echo "unknown --convert: $CONVERT" >&2; exit 1 ;;
esac

# --- encoder -----------------------------------------------------------------
# WebRTC needs: no B-frames, SPS/PPS repeated before every keyframe (so a new
# viewer can start decoding), and a profile every browser decodes.
case "$ENCODER" in
  x264)
    # vbv-buf-capacity (ms) caps how far a frame can overshoot the bitrate.
    # Small = keyframes don't burst the radio link, at some quality cost.
    ENC="x264enc tune=zerolatency speed-preset=ultrafast bitrate=${BITRATE} key-int-max=${GOP} bframes=0 vbv-buf-capacity=200 threads=${THREADS} ! video/x-h264,profile=constrained-baseline"
    ;;
  nvv4l2)
    [[ "$CONVERT" == "vic" ]] || CROP="${CROP} ! nvvidconv ! video/x-raw(memory:NVMM),format=I420"
    ENC="nvv4l2h264enc bitrate=$((BITRATE * 1000)) control-rate=1 iframeinterval=${GOP} idrinterval=${GOP} insert-sps-pps=true profile=0 preset-level=1 maxperf-enable=true"
    ;;
  *) echo "unknown --encoder: $ENCODER" >&2; exit 1 ;;
esac

PIPELINE="${SRC} ! ${CROP} ! ${LEAKY} ! ${ENC} ! h264parse config-interval=-1 ! rtspclientsink location=${URL} protocols=tcp latency=0"

echo "source:  $([[ $TEST == 1 ]] && echo test pattern || echo "$DEVICE") ${SBS_W}x${SBS_H}@${FPS} -> left half ${OUT_W}x${OUT_H}"
echo "encoder: ${ENCODER} ${BITRATE} kbit/s, keyframe every ${GOP} frames, convert on ${CONVERT}"
echo "publish: ${URL}"
echo "pipeline: gst-launch-1.0 -e ${PIPELINE}"

[[ $DRY_RUN == 1 ]] && exit 0
set -f  # no glob expansion when word-splitting the pipeline
# shellcheck disable=SC2086  # the pipeline is meant to be word-split
exec gst-launch-1.0 -e ${PIPELINE}
