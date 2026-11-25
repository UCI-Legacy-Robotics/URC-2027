// StreamManager: subscribes to a camera image (raw or JPEG) topic, encodes the
// frames to H.264 with x264 and publishes them over RTSP to the rover's MediaMTX.
//
// Runs as a component so it can share a process with the camera driver and
// receive frames without serialization (see docs/rover/video-stream-manager.md).

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

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
#include "sensor_msgs/msg/image.hpp"

namespace video_stream_manager
{

using namespace std::chrono_literals;
using CompressedImage = sensor_msgs::msg::CompressedImage;
using Image = sensor_msgs::msg::Image;
using SteadyClock = std::chrono::steady_clock;

class StreamManager : public rclcpp::Node
{
public:
  explicit StreamManager(const rclcpp::NodeOptions & options)
  : Node("video_stream_manager", options)
  {
    gst_init(nullptr, nullptr);

    settings_.rtsp_url =
      declare_parameter<std::string>("rtsp_url", "rtsp://127.0.0.1:8554/rover/front");
    settings_.rtsp_protocols = declare_parameter<std::string>("rtsp_protocols", "tcp");
    settings_.bitrate_kbps = declare_parameter<int>("bitrate_kbps", 2000);
    settings_.fps = declare_parameter<int>("fps", 30);
    settings_.keyframe_interval = declare_parameter<int>("keyframe_interval", 0);
    settings_.x264_threads = declare_parameter<int>("x264_threads", 0);
    retry_period_ = std::chrono::duration_cast<SteadyClock::duration>(
      std::chrono::duration<double>(declare_parameter<double>("retry_period", 2.0)));

    // Keep only the newest frame: an old frame is worse than a dropped one.
    if (declare_parameter<bool>("jpeg_input", false)) {
      input_ = Input::Jpeg;
      sub_ = create_subscription<CompressedImage>(
        "image/compressed", rclcpp::SensorDataQoS(),
        [this](CompressedImage::UniquePtr msg) {on_jpeg(std::move(msg));});
    } else {
      sub_ = create_subscription<Image>(
        "image", rclcpp::SensorDataQoS(),
        [this](Image::UniquePtr msg) {on_image(std::move(msg));});
    }
    bus_timer_ = create_wall_timer(100ms, [this]() {poll_bus();});

    RCLCPP_INFO(
      get_logger(), "Encoding %s to %s at %d kbit/s", sub_->get_topic_name(),
      settings_.rtsp_url.c_str(), settings_.bitrate_kbps);
  }

  ~StreamManager() override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_pipeline();
  }

private:
  void on_image(Image::UniquePtr msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto format = gst_format_for_encoding(msg->encoding);
    if (!format) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "Unsupported image encoding '%s'",
        msg->encoding.c_str());
      return;
    }
    if (msg->width == 0 || msg->height == 0 ||
      msg->data.size() < static_cast<size_t>(msg->step) * msg->height)
    {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Dropping malformed image");
      return;
    }

    // The appsrc caps are fixed per pipeline, so a new size or format needs a new one.
    if (pipeline_ && (*format != format_ || msg->width != width_ || msg->height != height_)) {
      RCLCPP_INFO(get_logger(), "Image size or encoding changed, restarting the pipeline");
      stop_pipeline();
    }
    if (!ensure_pipeline(appsrc_caps(*format, msg->width, msg->height, settings_.fps))) {
      return;
    }
    format_ = *format;
    video_format_ = gst_video_format_from_string(format->c_str());
    width_ = msg->width;
    height_ = msg->height;
    push_frame(std::move(msg));
  }

  void on_jpeg(CompressedImage::UniquePtr msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (msg->format.find("jpeg") == std::string::npos) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "Expected a JPEG image, got format '%s'",
        msg->format.c_str());
      return;
    }
    if (msg->data.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Dropping empty image");
      return;
    }
    if (!ensure_pipeline(jpeg_appsrc_caps(settings_.fps))) {
      return;
    }
    push_jpeg(std::move(msg));
  }

  // Starts the pipeline unless it's running or the last attempt failed less
  // than retry_period ago. Returns whether it's running.
  bool ensure_pipeline(const std::string & caps)
  {
    if (pipeline_) {
      return true;
    }
    if (last_failure_ && SteadyClock::now() - *last_failure_ < retry_period_) {
      return false;
    }
    if (!start_pipeline(caps)) {
      last_failure_ = SteadyClock::now();
      return false;
    }
    return true;
  }

  bool start_pipeline(const std::string & caps_string)
  {
    const std::string description = pipeline_description(settings_, input_);
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

    GstElement * appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstCaps * caps = gst_caps_from_string(caps_string.c_str());
    gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
    gst_caps_unref(caps);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      RCLCPP_ERROR(get_logger(), "Can't start the pipeline");
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(appsrc);
      gst_object_unref(pipeline);
      return false;
    }

    pipeline_ = pipeline;
    appsrc_ = appsrc;
    bus_ = gst_element_get_bus(pipeline);
    RCLCPP_INFO(
      get_logger(), "Streaming %s to %s", caps_string.c_str(), settings_.rtsp_url.c_str());
    RCLCPP_DEBUG(get_logger(), "Pipeline: %s", description.c_str());
    return true;
  }

  void stop_pipeline()
  {
    if (!pipeline_) {
      return;
    }
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(bus_);
    gst_object_unref(appsrc_);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appsrc_ = nullptr;
    bus_ = nullptr;
  }

  // Hands the message's pixel data to GStreamer without copying. The buffer
  // owns the message and deletes it once the encoder is done with the frame.
  void push_frame(Image::UniquePtr msg)
  {
    const gsize size = static_cast<gsize>(msg->step) * msg->height;
    guint8 * data = msg->data.data();
    const gsize max_size = msg->data.size();
    const guint width = msg->width;
    const guint height = msg->height;
    // Rows may be padded, so tell videoconvert the real stride.
    gsize offsets[GST_VIDEO_MAX_PLANES] = {0};
    gint strides[GST_VIDEO_MAX_PLANES] = {static_cast<gint>(msg->step)};

    GstBuffer * buffer = gst_buffer_new_wrapped_full(
      GST_MEMORY_FLAG_READONLY, data, max_size, 0, size, msg.release(),
      [](gpointer image) {delete static_cast<Image *>(image);});
    gst_buffer_add_video_meta_full(
      buffer, GST_VIDEO_FRAME_FLAG_NONE, video_format_, width, height, 1, offsets, strides);
    push_buffer(buffer);
  }

  // Hands the JPEG bytes to GStreamer without copying, like push_frame.
  void push_jpeg(CompressedImage::UniquePtr msg)
  {
    guint8 * data = msg->data.data();
    const gsize size = msg->data.size();
    GstBuffer * buffer = gst_buffer_new_wrapped_full(
      GST_MEMORY_FLAG_READONLY, data, size, 0, size, msg.release(),
      [](gpointer image) {delete static_cast<CompressedImage *>(image);});
    push_buffer(buffer);
  }

  void push_buffer(GstBuffer * buffer)
  {
    // Takes ownership of the buffer whatever the result.
    const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (ret != GST_FLOW_OK) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "appsrc refused a frame: %s", gst_flow_get_name(ret));
    }
  }

  // Errors (MediaMTX down or restarted, encoder failure, ...) tear the
  // pipeline down. on_image rebuilds it after retry_period.
  void poll_bus()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!bus_) {
      return;
    }

    bool failed = false;
    while (!failed) {
      GstMessage * msg = gst_bus_pop_filtered(
        bus_, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
        GST_MESSAGE_EOS));
      if (!msg) {
        break;
      }
      GError * error = nullptr;
      gchar * debug = nullptr;
      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error(msg, &error, &debug);
          RCLCPP_ERROR(
            get_logger(), "%s: %s (%s)", GST_OBJECT_NAME(msg->src), error->message,
            debug ? debug : "no details");
          failed = true;
          break;
        case GST_MESSAGE_WARNING:
          gst_message_parse_warning(msg, &error, &debug);
          RCLCPP_WARN(get_logger(), "%s: %s", GST_OBJECT_NAME(msg->src), error->message);
          break;
        case GST_MESSAGE_EOS:
          RCLCPP_WARN(get_logger(), "Pipeline ended");
          failed = true;
          break;
        default:
          break;
      }
      if (error) {
        g_error_free(error);
      }
      g_free(debug);
      gst_message_unref(msg);
    }

    if (failed) {
      stop_pipeline();
      last_failure_ = SteadyClock::now();
      RCLCPP_WARN(
        get_logger(), "Restarting the pipeline in %.1f s",
        std::chrono::duration<double>(retry_period_).count());
    }
  }

  EncoderSettings settings_;
  Input input_ = Input::Raw;
  SteadyClock::duration retry_period_;

  rclcpp::SubscriptionBase::SharedPtr sub_;
  rclcpp::TimerBase::SharedPtr bus_timer_;

  // Guards everything below. The subscription and the bus timer can run on
  // different threads in a multi-threaded container.
  std::mutex mutex_;
  GstElement * pipeline_ = nullptr;
  GstElement * appsrc_ = nullptr;
  GstBus * bus_ = nullptr;
  std::string format_;
  GstVideoFormat video_format_ = GST_VIDEO_FORMAT_UNKNOWN;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  std::optional<SteadyClock::time_point> last_failure_;
};

}  // namespace video_stream_manager

RCLCPP_COMPONENTS_REGISTER_NODE(video_stream_manager::StreamManager)
