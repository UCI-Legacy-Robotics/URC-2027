#include "pipeline.hpp"

#include <sstream>

namespace mjpeg_camera
{

std::string pipeline_description(const CaptureSettings & s)
{
  std::ostringstream p;
  p << "v4l2src device=\"" << s.device << "\""
    << " ! image/jpeg,width=" << s.width << ",height=" << s.height
    << ",framerate=" << s.camera_fps << "/1";
  // Some cameras only offer high frame rates (the OV9281 starts at 60 fps), so
  // drop frames down to the published rate. drop-only never duplicates one.
  if (s.fps < s.camera_fps) {
    p << " ! videorate drop-only=true ! image/jpeg,framerate=" << s.fps << "/1";
  }
  // Keep only the newest frame and never pace to the clock: latency over smoothness.
  p << " ! appsink name=sink max-buffers=1 drop=true sync=false";
  return p.str();
}

}  // namespace mjpeg_camera
