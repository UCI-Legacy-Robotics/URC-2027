#include <gtest/gtest.h>

#include <string>

#include "pipeline.hpp"

using mjpeg_camera::CaptureSettings;
using mjpeg_camera::pipeline_description;

namespace
{

CaptureSettings default_settings()
{
  return {"/dev/v4l/by-id/usb-cam-video-index0", 1280, 720, 60, 30};
}

bool contains(const std::string & haystack, const std::string & needle)
{
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(PipelineDescription, OpensDeviceInMjpeg)
{
  const std::string p = pipeline_description(default_settings());
  EXPECT_TRUE(contains(p, "v4l2src device=\"/dev/v4l/by-id/usb-cam-video-index0\""));
  EXPECT_TRUE(contains(p, "image/jpeg,width=1280,height=720,framerate=60/1"));
  EXPECT_TRUE(contains(p, "appsink name=sink"));
  EXPECT_FALSE(contains(p, "jpegdec"));
}

TEST(PipelineDescription, DropsToPublishedRate)
{
  EXPECT_TRUE(
    contains(
      pipeline_description(default_settings()),
      "videorate drop-only=true ! image/jpeg,framerate=30/1"));
}

TEST(PipelineDescription, NoVideorateAtCameraRate)
{
  CaptureSettings s = default_settings();
  s.fps = s.camera_fps;
  EXPECT_FALSE(contains(pipeline_description(s), "videorate"));
}
