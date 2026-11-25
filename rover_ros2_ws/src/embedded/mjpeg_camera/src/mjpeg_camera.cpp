// MjpegCamera: captures a UVC camera's MJPEG stream with GStreamer and
// publishes each JPEG, undecoded, as sensor_msgs/CompressedImage.
//
// Runs as a component so it can share a process with video_stream_manager
// (jpeg_input) and hand it frames without serialization. When the camera fails
// or is unplugged it reopens it after retry_period instead of exiting, so the
// container never has to be restarted (see docs/rover/video-stream-manager.md).

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "pipeline.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/compressed_image.hpp"

namespace mjpeg_camera
{

using namespace std::chrono_literals;
using CompressedImage = sensor_msgs::msg::CompressedImage;
using SteadyClock = std::chrono::steady_clock;

class MjpegCamera : public rclcpp::Node
{
public:
  explicit MjpegCamera(const rclcpp::NodeOptions & options)
  : Node("mjpeg_camera", options)
  {
    gst_init(nullptr, nullptr);

    settings_.device = declare_parameter<std::string>("device", "/dev/video0");
    settings_.width = declare_parameter<int>("width", 1280);
    settings_.height = declare_parameter<int>("height", 720);
    settings_.camera_fps = declare_parameter<int>("camera_fps", 30);
    settings_.fps = declare_parameter<int>("fps", 30);
    frame_id_ = declare_parameter<std::string>("frame_id", "camera");
    retry_period_ = std::chrono::duration_cast<SteadyClock::duration>(
      std::chrono::duration<double>(declare_parameter<double>("retry_period", 2.0)));

    // Keep only the newest frame: an old frame is worse than a dropped one.
    pub_ = create_publisher<CompressedImage>("image_raw/compressed", rclcpp::SensorDataQoS());
    poll_timer_ = create_wall_timer(100ms, [this]() {poll();});

    RCLCPP_INFO(
      get_logger(), "Publishing %s from %s, %dx%d at %d fps", pub_->get_topic_name(),
      settings_.device.c_str(), settings_.width, settings_.height, settings_.fps);
  }

  ~MjpegCamera() override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_pipeline();
  }

private:
  // Opens the camera if it's closed, and closes it on a pipeline error so the
  // next poll after retry_period reopens it.
  void poll()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
      if (last_failure_ && SteadyClock::now() - *last_failure_ < retry_period_) {
        return;
      }
      if (!start_pipeline()) {
        last_failure_ = SteadyClock::now();
      }
      return;
    }
    if (const auto error = pop_bus_error()) {
      RCLCPP_ERROR(get_logger(), "%s", error->c_str());
      stop_pipeline();
      last_failure_ = SteadyClock::now();
      RCLCPP_WARN(
        get_logger(), "Reopening the camera in %.1f s",
        std::chrono::duration<double>(retry_period_).count());
    }
  }

  bool start_pipeline()
  {
    const std::string description = pipeline_description(settings_);
    GError * error = nullptr;
    GstElement * pipeline = gst_parse_launch(description.c_str(), &error);
    if (error) {
      RCLCPP_ERROR(get_logger(), "Can't build the pipeline: %s", error->message);
      g_error_free(error);
      if (pipeline) {
        gst_object_unref(pipeline);
      }
      return false;
    }

    GstElement * sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &MjpegCamera::on_new_sample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
    gst_object_unref(sink);

    pipeline_ = pipeline;
    bus_ = gst_element_get_bus(pipeline);
    // v4l2src opens the device here, so a missing camera fails synchronously.
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      const auto bus_error = pop_bus_error();
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 10000, "Can't open %s: %s. Retrying every %.1f s",
        settings_.device.c_str(), bus_error ? bus_error->c_str() : "unknown error",
        std::chrono::duration<double>(retry_period_).count());
      stop_pipeline();
      return false;
    }
    RCLCPP_INFO(get_logger(), "Opened %s", settings_.device.c_str());
    RCLCPP_DEBUG(get_logger(), "Pipeline: %s", description.c_str());
    return true;
  }

  void stop_pipeline()
  {
    if (!pipeline_) {
      return;
    }
    // Joins the streaming thread, so on_new_sample isn't running afterwards.
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(bus_);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    bus_ = nullptr;
  }

  // The first error or end-of-stream on the bus, as a log message. Logs
  // warnings on the way.
  std::optional<std::string> pop_bus_error()
  {
    while (GstMessage * msg = gst_bus_pop_filtered(
        bus_, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
        GST_MESSAGE_EOS)))
    {
      std::optional<std::string> result;
      GError * error = nullptr;
      gchar * debug = nullptr;
      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error(msg, &error, &debug);
          result = std::string(GST_OBJECT_NAME(msg->src)) + ": " + error->message +
            " (" + (debug ? debug : "no details") + ")";
          break;
        case GST_MESSAGE_WARNING:
          gst_message_parse_warning(msg, &error, &debug);
          RCLCPP_WARN(get_logger(), "%s: %s", GST_OBJECT_NAME(msg->src), error->message);
          break;
        case GST_MESSAGE_EOS:
          result = "The camera stream ended";
          break;
        default:
          break;
      }
      if (error) {
        g_error_free(error);
      }
      g_free(debug);
      gst_message_unref(msg);
      if (result) {
        return result;
      }
    }
    return std::nullopt;
  }

  // Runs on GStreamer's streaming thread for every frame.
  static GstFlowReturn on_new_sample(GstAppSink * sink, gpointer user_data)
  {
    auto * self = static_cast<MjpegCamera *>(user_data);
    GstSample * sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
      return GST_FLOW_EOS;
    }
    GstBuffer * buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      auto msg = std::make_unique<CompressedImage>();
      msg->header.stamp = self->now();
      msg->header.frame_id = self->frame_id_;
      msg->format = "jpeg";
      // The frame's only copy: the message has to own its bytes. Publishing the
      // unique_ptr then hands it to an intra-process subscriber without another.
      msg->data.assign(map.data, map.data + map.size);
      gst_buffer_unmap(buffer, &map);
      self->pub_->publish(std::move(msg));
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  CaptureSettings settings_;
  std::string frame_id_;
  SteadyClock::duration retry_period_;

  rclcpp::Publisher<CompressedImage>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr poll_timer_;

  // Guards everything below. on_new_sample doesn't take it: stop_pipeline
  // waits for the streaming thread while holding it.
  std::mutex mutex_;
  GstElement * pipeline_ = nullptr;
  GstBus * bus_ = nullptr;
  std::optional<SteadyClock::time_point> last_failure_;
};

}  // namespace mjpeg_camera

RCLCPP_COMPONENTS_REGISTER_NODE(mjpeg_camera::MjpegCamera)
