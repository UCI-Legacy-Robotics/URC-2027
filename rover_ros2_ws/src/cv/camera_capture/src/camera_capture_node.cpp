#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <mutex>

#include "pano_interfaces/srv/save_frame.hpp"
#include "cv_bridge/cv_bridge.h"
#include "image_transport/image_transport.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

class CameraCaptureNode : public rclcpp::Node {
public:
  CameraCaptureNode() : Node("camera_capture_node") {
    image_topic_ = declare_parameter<std::string>(
        "image_topic", "/zed2/zed_node/left/image_rect_color");
    output_dir_ = declare_parameter<std::string>("output_dir", "");

    image_sub_ = image_transport::create_subscription(
        static_cast<rclcpp::Node*>(this), image_topic_,
        [this](const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
          std::lock_guard<std::mutex> lock(frame_mutex_);
          latest_frame_ = msg;
        }, "raw");

    save_service_ = create_service<pano_interfaces::srv::SaveFrame>(
        "~/save_frame",
        [this](const std::shared_ptr<pano_interfaces::srv::SaveFrame::Request> req,
               std::shared_ptr<pano_interfaces::srv::SaveFrame::Response> res) {
          handleSaveFrame(req, res);
        });

    RCLCPP_INFO(get_logger(),
        "camera_capture_node ready — topic: '%s', output_dir: '%s'.",
        image_topic_.c_str(), output_dir_.c_str());
  }

private:
  void handleSaveFrame(
      const std::shared_ptr<pano_interfaces::srv::SaveFrame::Request> request,
      std::shared_ptr<pano_interfaces::srv::SaveFrame::Response> response) {

    if (output_dir_.empty()) {
      response->success = false;
      response->message = "output_dir parameter is not set.";
      return;
    }

    sensor_msgs::msg::Image::ConstSharedPtr frame;
    {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      frame = latest_frame_;
    }

    if (!frame) {
      response->success = false;
      response->message = "No frame received yet.";
      return;
    }

    // Build filename: frame_045.00.jpg
    std::ostringstream name;
    name << "frame_" << std::fixed << std::setw(6) << std::setfill('0')
         << std::setprecision(2) << request->angle;
    const std::string stem = name.str();
    const std::string jpg_path  = output_dir_ + "/" + stem + ".jpg";
    const std::string json_path = output_dir_ + "/" + stem + ".json";

    // Convert and save image
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(frame, "bgr8");
    } catch (const cv_bridge::Exception& e) {
      response->success = false;
      response->message = std::string("cv_bridge: ") + e.what();
      return;
    }

    fs::create_directories(output_dir_);
    if (!cv::imwrite(jpg_path, cv_ptr->image)) {
      response->success = false;
      response->message = "Failed to write image to " + jpg_path;
      return;
    }

    // Write sidecar JSON
    std::ofstream json(json_path);
    json << "{\n"
         << "  \"angle\": " << request->angle << ",\n"
         << "  \"timestamp\": " << frame->header.stamp.sec
         << "." << std::setw(9) << std::setfill('0') << frame->header.stamp.nanosec << "\n"
         << "}\n";
    json.close();

    response->success  = true;
    response->filepath = jpg_path;
    response->message  = "Saved " + jpg_path;

    RCLCPP_INFO(get_logger(), "Saved frame at %.2f° → %s",
        request->angle, jpg_path.c_str());
  }

  std::string image_topic_;
  std::string output_dir_;

  sensor_msgs::msg::Image::ConstSharedPtr latest_frame_;
  std::mutex frame_mutex_;

  image_transport::Subscriber image_sub_;
  rclcpp::Service<pano_interfaces::srv::SaveFrame>::SharedPtr save_service_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CameraCaptureNode>());
  rclcpp::shutdown();
}