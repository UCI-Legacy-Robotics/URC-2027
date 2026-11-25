#include <gtest/gtest.h>

#include <string>

#include "pipeline.hpp"

using video_stream_manager::appsrc_caps;
using video_stream_manager::EncoderSettings;
using video_stream_manager::gst_format_for_encoding;
using video_stream_manager::pipeline_description;

namespace
{

EncoderSettings default_settings()
{
  return {"rtsp://127.0.0.1:8554/rover/front", "tcp", 2000, 30, 0, 0};
}

bool contains(const std::string & haystack, const std::string & needle)
{
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(GstFormat, MapsZedAndCommonEncodings)
{
  EXPECT_EQ(gst_format_for_encoding("bgra8"), "BGRA");  // what the ZED wrapper publishes
  EXPECT_EQ(gst_format_for_encoding("rgb8"), "RGB");
  EXPECT_EQ(gst_format_for_encoding("mono8"), "GRAY8");
  EXPECT_EQ(gst_format_for_encoding("yuv422_yuy2"), "YUY2");
}

TEST(GstFormat, RejectsUnsupportedEncodings)
{
  EXPECT_FALSE(gst_format_for_encoding("32FC1").has_value());  // depth images
  EXPECT_FALSE(gst_format_for_encoding("").has_value());
}

TEST(AppsrcCaps, DescribesFrames)
{
  EXPECT_EQ(
    appsrc_caps("BGRA", 1280, 720, 30),
    "video/x-raw,format=BGRA,width=1280,height=720,framerate=30/1");
}

TEST(PipelineDescription, UsesSettings)
{
  const std::string p = pipeline_description(default_settings());
  EXPECT_TRUE(contains(p, "appsrc name=src"));
  EXPECT_TRUE(contains(p, "bitrate=2000"));
  EXPECT_TRUE(contains(p, "location=\"rtsp://127.0.0.1:8554/rover/front\""));
  EXPECT_TRUE(contains(p, "protocols=tcp"));
}

TEST(PipelineDescription, IsWebRtcCompatible)
{
  const std::string p = pipeline_description(default_settings());
  EXPECT_TRUE(contains(p, "bframes=0"));
  EXPECT_TRUE(contains(p, "profile=constrained-baseline"));
  EXPECT_TRUE(contains(p, "config-interval=-1"));
}

TEST(PipelineDescription, KeyframeEverySecondByDefault)
{
  EncoderSettings s = default_settings();
  s.fps = 15;
  EXPECT_TRUE(contains(pipeline_description(s), "key-int-max=15"));

  s.keyframe_interval = 10;
  EXPECT_TRUE(contains(pipeline_description(s), "key-int-max=10"));
}
