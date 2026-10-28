#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <control_msgs/msg/joint_jog.hpp>
#include <moveit_msgs/srv/servo_command_type.hpp>

using namespace std::chrono_literals;
using ServoCommand = moveit_msgs::srv::ServoCommandType;
using Clock = std::chrono::steady_clock;

class ArmTeleop : public rclcpp::Node
{
public:
  ArmTeleop() : Node("arm_teleop")
  {
    joint_names_ = declare_parameter<std::vector<std::string>>(
      "joint_names", {"base", "shoulder", "elbow", "wrist_pitch", "wrist_roll"});
    frame_ = declare_parameter("command_frame", "base_link");
    enable_button_ = declare_parameter("enable_button", 9);
    mode_button_ = declare_parameter("mode_button", 6);
    axes_ = declare_parameter<std::vector<int64_t>>("axes", {0, 1, 2, 3});
    signs_ = declare_parameter<std::vector<double>>("axis_signs", {1., -1., 1., -1.});
    wrist_negative_ = declare_parameter("wrist_negative_button", 13);
    wrist_positive_ = declare_parameter("wrist_positive_button", 14);
    yaw_positive_ = declare_parameter("yaw_positive_button", 11);
    yaw_negative_ = declare_parameter("yaw_negative_button", 12);
    deadzone_ = declare_parameter("deadzone", 0.1);
    timeout_ = declare_parameter("joy_timeout", 0.5);
    linear_speed_ = declare_parameter("linear_speed", 0.15);   // original value 0.15
    angular_speed_ = declare_parameter("angular_speed", 0.3);  // original value 0.3
    joint_speed_ = declare_parameter("joint_speed", 0.5);      // original value 0.5

    if (joint_names_.size() != 5 || (axes_.size() != 4 && axes_.size() != 5) || signs_.size() != axes_.size() ||
        deadzone_ < 0.0 || deadzone_ >= 1.0 || timeout_ <= 0.0 ||
        !std::isfinite(deadzone_) || !std::isfinite(timeout_) ||
        !std::isfinite(linear_speed_) || linear_speed_ <= 0.0 ||
        !std::isfinite(angular_speed_) || angular_speed_ <= 0.0 ||
        !std::isfinite(joint_speed_) || joint_speed_ <= 0.0) {
      throw std::runtime_error("Check joint names, four or five matching axes/signs, deadzone, timeout and speeds in teleop.yaml");
    }
    for (double sign : signs_) {
      if (sign != 1.0 && sign != -1.0) {
        throw std::runtime_error("axis_signs must contain only 1.0 or -1.0");
      }
    }

    twist_publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("/arm_teleop/delta_twist_cmds", 10);
    joint_publisher_ = create_publisher<control_msgs::msg::JointJog>("/arm_teleop/delta_joint_cmds", 10);
    mode_client_ = create_client<ServoCommand>("/servo_node/switch_command_type");
    joy_subscription_ = create_subscription<sensor_msgs::msg::Joy>(
      "/joy", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Joy::ConstSharedPtr msg) { read_joystick(*msg); });
    timer_ = create_wall_timer(20ms, [this]() { update(); });
    RCLCPP_INFO(get_logger(), "Starting in joint mode. Release enable at neutral before moving.");
  }

private:
  bool valid_input(const sensor_msgs::msg::Joy & joy) const
  {
    for (auto index : axes_) {
      if (index < 0 || static_cast<size_t>(index) >= joy.axes.size() ||
          !std::isfinite(joy.axes[index])) {
        return false;
      }
    }
    for (int index : {enable_button_, mode_button_, yaw_positive_, yaw_negative_}) {
      if (index < 0 || static_cast<size_t>(index) >= joy.buttons.size()) {
        return false;
      }
    }
    if (axes_.size() == 4) {
      for (int index : {wrist_negative_, wrist_positive_}) {
        if (index < 0 || static_cast<size_t>(index) >= joy.buttons.size()) {
          return false;
        }
      }
    }
    return true;
  }

  void read_joystick(const sensor_msgs::msg::Joy & joy)
  {
    if (!valid_input(joy)) {
      have_input_ = false;
      armed_ = false;
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
        "Invalid /joy message. Check indices in teleop.yaml.");
      return;
    }

    const auto received = Clock::now();
    if (std::chrono::duration<double>(received - last_input_).count() > timeout_) {
      armed_ = false;
    }
    last_input_ = received;
    have_input_ = true;
    enable_ = joy.buttons[enable_button_] != 0;

    for (size_t i = 0; i < axes_.size(); ++i) {
      const double value = std::clamp(static_cast<double>(joy.axes[axes_[i]]), -1.0, 1.0);
      inputs_[i] = std::abs(value) <= deadzone_ ? 0.0 : signs_[i] * value;
    }
    // Four-axis controllers use buttons for wrist roll. Five-axis controllers
    // already supplied inputs_[4] above (e.g. the Xbox D-pad axis).
    if (axes_.size() == 4) {
      inputs_[4] = static_cast<double>((joy.buttons[wrist_positive_] != 0) -
                                       (joy.buttons[wrist_negative_] != 0));
    }
    yaw_ = static_cast<double>((joy.buttons[yaw_positive_] != 0) -
                                (joy.buttons[yaw_negative_] != 0));
    const bool neutral = std::all_of(inputs_.begin(), inputs_.end(),
      [](double value) { return value == 0.0; }) && yaw_ == 0.0;
    const bool mode_pressed = joy.buttons[mode_button_] != 0;

    if (mode_pressed && !previous_mode_button_ && !enable_ && neutral && mode_ready_) {
      publish_command(false);
      cartesian_ = !cartesian_;
      mode_ready_ = false;
      armed_ = false;
      next_mode_attempt_ = received + 200ms;
    }
    previous_mode_button_ = mode_pressed;

    if (mode_ready_ && !enable_ && neutral) {
      armed_ = true;
    }
  }

  void request_mode()
  {
    if (!mode_client_->service_is_ready()) {
      return;
    }
    auto request = std::make_shared<ServoCommand::Request>();
    request->command_type = cartesian_ ? ServoCommand::Request::TWIST :
      ServoCommand::Request::JOINT_JOG;
    mode_pending_ = true;
    mode_request_time_ = Clock::now();
    auto pending = mode_client_->async_send_request(request,
      [this](rclcpp::Client<ServoCommand>::SharedFuture response) {
        mode_pending_ = false;
        mode_ready_ = response.get()->success;
        armed_ = false;
        RCLCPP_INFO(get_logger(), "Servo mode %s: %s. Release enable at neutral.",
          cartesian_ ? "Cartesian" : "joint", mode_ready_ ? "ready" : "rejected");
      });
    mode_request_id_ = pending.request_id;
    next_mode_attempt_ = Clock::now() + 1s;
  }

  void update()
  {
    const auto time = Clock::now();
    if (mode_pending_ && time - mode_request_time_ > 2s) {
      mode_client_->remove_pending_request(mode_request_id_);
      mode_pending_ = false;
      RCLCPP_WARN(get_logger(), "Servo mode request timed out; retrying.");
    }
    if (!mode_ready_) {
      if (!mode_pending_ && time >= next_mode_attempt_) {
        request_mode();
      }
      return;
    }
    const bool fresh = have_input_ &&
      std::chrono::duration<double>(time - last_input_).count() <= timeout_;
    if (!fresh) {
      armed_ = false;
    }
    publish_command(fresh && armed_ && enable_);
  }

  void publish_command(bool moving)
  {
    const double scale = moving ? 1.0 : 0.0;
    if (cartesian_) {
      geometry_msgs::msg::TwistStamped command;
      command.header.stamp = now();
      command.header.frame_id = frame_;
      command.twist.linear.x = scale * linear_speed_ * inputs_[0];
      command.twist.linear.y = scale * linear_speed_ * inputs_[1];
      command.twist.linear.z = scale * linear_speed_ * inputs_[2];
      command.twist.angular.x = scale * angular_speed_ * inputs_[3];
      command.twist.angular.y = scale * angular_speed_ * inputs_[4];
      command.twist.angular.z = scale * angular_speed_ * yaw_;
      twist_publisher_->publish(command);
    } else {
      control_msgs::msg::JointJog command;
      command.header.stamp = now();
      command.header.frame_id = frame_;
      command.joint_names = joint_names_;
      for (double input : inputs_) {
        command.velocities.push_back(scale * joint_speed_ * input);
      }
      joint_publisher_->publish(command);
    }
  }

  std::vector<std::string> joint_names_;
  std::string frame_;
  std::vector<int64_t> axes_;
  std::vector<double> signs_;
  int enable_button_, mode_button_, wrist_negative_, wrist_positive_, yaw_positive_, yaw_negative_;
  double deadzone_, timeout_, linear_speed_, angular_speed_, joint_speed_;
  std::array<double, 5> inputs_{};
  double yaw_ = 0.0;
  bool have_input_ = false, enable_ = false, armed_ = false;
  bool cartesian_ = false, previous_mode_button_ = false;
  bool mode_ready_ = false, mode_pending_ = false;
  int64_t mode_request_id_ = 0;
  Clock::time_point last_input_{}, next_mode_attempt_{}, mode_request_time_{};

  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_subscription_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr twist_publisher_;
  rclcpp::Publisher<control_msgs::msg::JointJog>::SharedPtr joint_publisher_;
  rclcpp::Client<ServoCommand>::SharedPtr mode_client_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ArmTeleop>());
  rclcpp::shutdown();
  return 0;
}
