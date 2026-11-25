// Builds the GStreamer pipeline description used by StreamManager.
// Kept free of ROS and GStreamer calls so it can be unit tested.

#pragma once

#include <optional>
#include <string>

namespace video_stream_manager
{

// What the node subscribes to and pushes into the appsrc.
enum class Input
{
  Raw,   // sensor_msgs/Image, converted to I420
  Jpeg,  // sensor_msgs/CompressedImage holding JPEG, decoded to I420
};

struct EncoderSettings
{
  std::string rtsp_url;
  std::string rtsp_protocols;  // rtspclientsink "protocols", e.g. "tcp"
  int bitrate_kbps;
  int fps;                // expected input frame rate
  int keyframe_interval;  // frames between keyframes; <= 0 means one per second (= fps)
  int x264_threads;       // 0 = x264 picks
};

// GStreamer raw video format for a sensor_msgs/Image encoding,
// or nullopt if the encoding isn't supported.
std::optional<std::string> gst_format_for_encoding(const std::string & encoding);

// Caps string for the appsrc, describing the raw frames the node will push.
std::string appsrc_caps(const std::string & gst_format, int width, int height, int fps);

// Caps string for the appsrc when it's fed JPEG images. jpegdec reads the size
// from each image.
std::string jpeg_appsrc_caps(int fps);

// gst_parse_launch description: appsrc named "src" -> (jpegdec) -> I420 -> x264
// -> RTSP publish. Encoder settings match prototypes/video/rover/publish-zed.sh.
std::string pipeline_description(const EncoderSettings & settings, Input input);

}  // namespace video_stream_manager
