#include <memory>
#include <functional>
#include "sensor_msgs/msg/joy.hpp"
#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"
#define LINEAR_SENS 0.8
#define ANGULAR_SENS 0.3

// Inherit from rclcpp::Node to define your own ROS 2 node.
class ArcadeNode : public rclcpp::Node
{
public:
  ArcadeNode() : Node("arcade")
  {
    velocity_publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("/diff_cont/cmd_vel", 10);

    // Publish a velocity command whenever joystick input arrives.
    joy_subscription_ = create_subscription<sensor_msgs::msg::Joy>(
        "/joy", 10, std::bind(&ArcadeNode::joy_callback, this, std::placeholders::_1));
  }

private:

  void joy_callback(sensor_msgs::msg::Joy::ConstSharedPtr msg){

    if (msg->axes.size() < 4) {
      return;
    }

    geometry_msgs::msg::TwistStamped command;
    command.header.stamp = now();
    command.twist.linear.x = msg->axes[1] * LINEAR_SENS;
    command.twist.angular.z = msg->axes[3] * ANGULAR_SENS;

    velocity_publisher_->publish(command);
    

  }
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr velocity_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ArcadeNode>();
  rclcpp::spin(node);  // Process callbacks until shutdown (Ctrl+C).
  rclcpp::shutdown();
  return 0;
}
