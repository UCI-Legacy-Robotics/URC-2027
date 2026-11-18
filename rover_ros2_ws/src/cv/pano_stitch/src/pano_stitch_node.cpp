#include <memory>
#include <string>
#include <filesystem>

#include "cv_bridge/cv_bridge.h"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/header.hpp"
#include "pano_interfaces/srv/stitch_folder.hpp"
#include <opencv2/imgcodecs.hpp>


#include "stitcher.hpp"

namespace fs = std::filesystem;

// Buffers incoming frames from a ZED2 image topic.
// Call ~/stitch to stitch whatever is in the buffer into a panorama.
// Call ~/clear_buffer to reset before a new pan.
class PanoStitchNode : public rclcpp::Node {
public:
  PanoStitchNode() : Node("pano_stitch_node") {

    pano_pub_ = create_publisher<sensor_msgs::msg::Image>("~/panorama", 10);

    stitch_service_ = create_service<pano_interfaces::srv::StitchFolder>(
        "~/stitch",
        [this](const std::shared_ptr<pano_interfaces::srv::StitchFolder::Request> req,
               std::shared_ptr<pano_interfaces::srv::StitchFolder::Response> res) {
          handleStitch(req, res);
        });

    RCLCPP_INFO(get_logger(),
        "pano_stitch_node ready. Call '~/stitch' with a folder_path to run.");
    
  }

private:
  void handleStitch(
    const std::shared_ptr<pano_interfaces::srv::StitchFolder::Request> request,
    std::shared_ptr<pano_interfaces::srv::StitchFolder::Response> response) {
    
    const std::string image_dir = request->folder_path;

    if(image_dir.empty()) {
      response->success = false;
      response->message = "No image_dir specified.";
      return;
    }
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(image_dir)) {
      if (entry.is_regular_file()) {
        paths.push_back(entry.path());
      }
    }

    std::sort(paths.begin(), paths.end());

    if (paths.size() < 2) {
      response->success = false;
      response->message = "Need at least 2 images in '" + image_dir +
                          "' (found " + std::to_string(paths.size()) + ").";
      return;
    }

    std::vector<cv::Mat> images;

    for (const auto& path : paths) {
      cv::Mat img = cv::imread(path.string());
      if (!img.empty()) {
        images.push_back(img);
      }
    }
    

    cv::Mat panorama = stitcher_.stitch(images);

    if (panorama.empty()) {
      response->success = false;
      response->message = "Stitching failed — tcheck image overlap.";
      return;
    }

    std_msgs::msg::Header header;
    header.stamp    = now();
    header.frame_id = "panorama";
    pano_pub_->publish(*cv_bridge::CvImage(header, "bgr8", panorama).toImageMsg());

    response->success = true;
    response->message = "Published " + std::to_string(panorama.cols) + "x" +
                        std::to_string(panorama.rows) + " panorama from " +
                        std::to_string(images.size()) + " images.";
  }

  pano_stitch::PanoStitcher stitcher_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pano_pub_;
  rclcpp::Service<pano_interfaces::srv::StitchFolder>::SharedPtr stitch_service_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PanoStitchNode>());
  rclcpp::shutdown();
}
