#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "cv_bridge/cv_bridge.h"
#include "image_transport/image_transport.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/header.hpp"
#include "std_srvs/srv/trigger.hpp"

#include "stitcher.hpp"

// Buffers incoming frames from a ZED2 image topic.
// Call ~/stitch to stitch whatever is in the buffer into a panorama.
// Call ~/clear_buffer to reset before a new pan.
class PanoStitchNode : public rclcpp::Node {
public:
  PanoStitchNode() : Node("pano_stitch_node") {
    image_topic_    = declare_parameter<std::string>("image_topic",
                          "/zed2/zed_node/left/image_rect_color");
    max_buffer_size_ = declare_parameter<int>("max_buffer_size", 8);

    image_sub_ = image_transport::create_subscription(
        this, image_topic_,
        [this](const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
          imageCallback(msg);
        }, "raw");

    pano_pub_ = image_transport::create_publisher(this, "~/panorama");

    stitch_service_ = create_service<std_srvs::srv::Trigger>(
        "~/stitch",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
               std::shared_ptr<std_srvs::srv::Trigger::Response> res) {
          handleStitch(req, res);
        });

    clear_service_ = create_service<std_srvs::srv::Trigger>(
        "~/clear_buffer",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
               std::shared_ptr<std_srvs::srv::Trigger::Response> res) {
          handleClear(req, res);
        });

    RCLCPP_INFO(get_logger(),
        "pano_stitch_node ready — topic: '%s', buffer: %d frames.",
        image_topic_.c_str(), max_buffer_size_);
  }

private:
  void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(msg, "bgr8");
    } catch (const cv_bridge::Exception& e) {
      RCLCPP_WARN(get_logger(), "cv_bridge: %s", e.what());
      return;
    }
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.push_back(cv_ptr->image.clone());
    if (static_cast<int>(buffer_.size()) > max_buffer_size_)
      buffer_.pop_front();
  }

  void handleStitch(
      const std::shared_ptr<std_srvs::srv::Trigger::Request>,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
    std::vector<cv::Mat> frames;
    {
      std::lock_guard<std::mutex> lock(buffer_mutex_);
      frames.assign(buffer_.begin(), buffer_.end());
    }

    if (frames.size() < 2) {
      response->success = false;
      response->message = "Need at least 2 frames (have " +
                          std::to_string(frames.size()) + ").";
      return;
    }

    cv::Mat panorama = stitcher_.stitch(frames);

    if (panorama.empty()) {
      response->success = false;
      response->message = "Stitching failed — try panning more slowly for better overlap.";
      return;
    }

    std_msgs::msg::Header header;
    header.stamp    = now();
    header.frame_id = "panorama";
    pano_pub_.publish(cv_bridge::CvImage(header, "bgr8", panorama).toImageMsg());

    response->success = true;
    response->message = "Published " + std::to_string(panorama.cols) + "x" +
                        std::to_string(panorama.rows) + " panorama from " +
                        std::to_string(frames.size()) + " frames.";
  }

  void handleClear(
      const std::shared_ptr<std_srvs::srv::Trigger::Request>,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
    response->success = true;
    response->message = "Buffer cleared.";
  }

  std::string image_topic_;
  int         max_buffer_size_;

  std::deque<cv::Mat>  buffer_;
  std::mutex           buffer_mutex_;
  pano_stitch::PanoStitcher stitcher_;

  image_transport::Subscriber image_sub_;
  image_transport::Publisher  pano_pub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stitch_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr clear_service_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PanoStitchNode>());
  rclcpp::shutdown();
}
