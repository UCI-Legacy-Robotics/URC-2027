#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.hpp>

#include <opencv2/opencv.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>

using namespace std::chrono_literals;

class WebcamNode : public rclcpp::Node
{
public:

    WebcamNode()
        : Node("webcam_node")
    {
        //declare ros parameters
        this->declare_parameter<int>("camera_index", 0);
        this->declare_parameter<int>("width", 640);
        this->declare_parameter<int>("height", 480);
        this->declare_parameter<int>("fps", 30);

        const int camera_index = this->get_parameter("camera_index").as_int();

        const int width = this->get_parameter("width").as_int();

        const int height = this->get_parameter("height").as_int();

        const int fps = this->get_parameter("fps").as_int();

        if (fps <= 0) {
            throw std::runtime_error("fps must be positive");
        }

        //create ros image publisher
        image_publisher_ = this->create_publisher<sensor_msgs::msg::Image>("/camera/image_raw", rclcpp::SensorDataQoS());

        //open webcam
        camera_.open(camera_index);

        if (!camera_.isOpened())
        {
            throw std::runtime_error(
                "Failed to open webcam"
            );
        }

        //configure capture resolution and fps
        camera_.set(cv::CAP_PROP_FRAME_WIDTH, width);
        camera_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        camera_.set(cv::CAP_PROP_FPS, fps);

        //create timer for frame capture
        const auto period = std::chrono::duration<double>(1.0 / fps);

        timer_ = this->create_wall_timer(period, std::bind(&WebcamNode::capture_frame, this));

        RCLCPP_INFO(
            this->get_logger(),
            "Webcam initialized successfully"
        );
    }

    ~WebcamNode()
    {
        camera_.release();
    }

private:

    void capture_frame()
    {
        cv::Mat frame;

        //capture frame from webcam
         if (!camera_.read(frame) || frame.empty())
        {
            RCLCPP_WARN(
                this->get_logger(),
                "Failed to capture frame"
            );

            return;
        }

        //ros message header
        std_msgs::msg::Header header;

        header.stamp = this->get_clock()->now();
        header.frame_id = "camera_link";

        //convert opencv image into ros image msg
         auto image_msg = cv_bridge::CvImage(
            header,
            "bgr8",
            frame
        ).toImageMsg();

        //publish frame
        image_publisher_->publish(*image_msg);
    }

    cv::VideoCapture camera_;

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr
        image_publisher_;

    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<WebcamNode>();
        rclcpp::spin(node);
    }
    
    catch (const std::exception &e) {
        RCLCPP_ERROR(
            rclcpp::get_logger("webcam_node"),
            "Camera node error: %s",
            e.what()
        );
    }

    rclcpp::shutdown();

    return 0;
}