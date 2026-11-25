#include "pipeline.hpp"

#include <map>
#include <sstream>
#include <string>

namespace video_stream_manager
{

std::optional<std::string> gst_format_for_encoding(const std::string & encoding)
{
  // Packed, single-plane formats only: each frame is wrapped as one buffer.
  static const std::map<std::string, std::string> formats = {
    {"bgra8", "BGRA"},
    {"rgba8", "RGBA"},
    {"bgr8", "BGR"},
    {"rgb8", "RGB"},
    {"mono8", "GRAY8"},
    {"yuv422", "UYVY"},
    {"yuv422_yuy2", "YUY2"},
  };
  const auto it = formats.find(encoding);
  if (it == formats.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string appsrc_caps(const std::string & gst_format, int width, int height, int fps)
{
  std::ostringstream caps;
  caps << "video/x-raw,format=" << gst_format << ",width=" << width << ",height=" << height
       << ",framerate=" << fps << "/1";
  return caps.str();
}

std::string jpeg_appsrc_caps(int fps)
{
  return "image/jpeg,framerate=" + std::to_string(fps) + "/1";
}

std::string pipeline_description(const EncoderSettings & s, Input input)
{
  const int keyframe_interval = s.keyframe_interval > 0 ? s.keyframe_interval : s.fps;

  std::ostringstream p;
  // Live source, timestamped on arrival. Holds at most one frame and drops the
  // older one if the encoder falls behind, so latency stays flat under load.
  p << "appsrc name=src is-live=true format=time do-timestamp=true"
    << " max-buffers=1 leaky-type=downstream block=false";
  if (input == Input::Jpeg) {
    p << " ! jpegdec";
  }
  // A no-op when jpegdec already outputs I420 (4:2:0 JPEG).
  p << " ! videoconvert ! video/x-raw,format=I420"
    // WebRTC needs no B-frames and a profile every browser decodes.
    // vbv-buf-capacity (ms) stops keyframes from bursting the radio link.
    << " ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=" << s.bitrate_kbps
    << " key-int-max=" << keyframe_interval << " bframes=0 vbv-buf-capacity=200"
    << " threads=" << s.x264_threads
    << " ! video/x-h264,profile=constrained-baseline"
    // Repeat SPS/PPS before every keyframe so viewers can join mid-stream.
    << " ! h264parse config-interval=-1"
    // Sends on its own thread, so a slow RTSP write doesn't stall the encoder.
    << " ! queue"
    << " ! rtspclientsink location=\"" << s.rtsp_url << "\""
    << " protocols=" << s.rtsp_protocols << " latency=0";
  return p.str();
}

}  // namespace video_stream_manager
