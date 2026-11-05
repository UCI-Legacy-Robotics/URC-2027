#include <chrono>
#include <memory>
#include <functional>
#include "sensor_msgs/msg/joy.hpp"
#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#define LINEAR_SENS 0.5
#define ANGULAR_SENS 0.5

// Inherit from rclcpp::Node to define your own ROS 2 node.
class ArcadeNode : public rclcpp::Node
{
public:
  ArcadeNode() : Node("arcade")
  {
    velocity_publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("/diff_cont/vel_cmd", 10);

    // Publish a sample command once per second; all other components remain zero.
    joy_subscription_ = create_subscription<sensor_msgs::msg::Joy>(
        "/joy", 10, std::bind(&ArcadeNode::joy_callback, this, std::placeholders::_1));
  }

private:

  void joy_callback(sensor_msgs::msg::Joy::ConstSharedPtr msg){

    geometry_msgs::msg::TwistStamped command;
    command.linear.x = msg->axes[1] * LINEAR_SENS;
    command.angular.z = msg->axes[2] * ANGULAR_SENS;

    velocity_publisher_->publish(command);
    

  }
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr velocity_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_subscription_;
  // Keep the timer alive for as long as this node exists.
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ArcadeNode>();
  rclcpp::spin(node);  // Process callbacks until shutdown (Ctrl+C).
  rclcpp::shutdown();
  return 0;
}
