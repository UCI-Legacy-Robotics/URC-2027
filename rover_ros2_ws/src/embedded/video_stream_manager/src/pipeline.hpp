// Builds the GStreamer pipeline description used by StreamManager.
// Kept free of ROS and GStreamer calls so it can be unit tested.

#pragma once

#include <optional>
#include <string>

namespace video_stream_manager
{

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

// Caps string for the appsrc, describing the frames the node will push.
std::string appsrc_caps(const std::string & gst_format, int width, int height, int fps);

// gst_parse_launch description: appsrc named "src" -> I420 -> x264 -> RTSP publish.
// Encoder settings match prototypes/video/rover/publish-zed.sh.
std::string pipeline_description(const EncoderSettings & settings);

}  // namespace video_stream_manager
