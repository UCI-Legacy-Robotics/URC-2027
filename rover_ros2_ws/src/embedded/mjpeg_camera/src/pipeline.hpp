// Builds the GStreamer pipeline description used by MjpegCamera.
// Kept free of ROS and GStreamer calls so it can be unit tested.

#pragma once

#include <string>

namespace mjpeg_camera
{

struct CaptureSettings
{
  std::string device;  // V4L2 device, e.g. /dev/v4l/by-id/...
  int width;
  int height;
  int camera_fps;  // a frame rate the camera offers for width x height
  int fps;         // published frame rate, at most camera_fps
};

// gst_parse_launch description: v4l2src -> MJPEG caps -> videorate -> appsink
// named "sink". The JPEGs are never decoded.
std::string pipeline_description(const CaptureSettings & settings);

}  // namespace mjpeg_camera
