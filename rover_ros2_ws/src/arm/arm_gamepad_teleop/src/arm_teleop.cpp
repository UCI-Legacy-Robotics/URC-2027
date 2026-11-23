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
#include <sensor_msgs/msg/joint_state.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
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
        "joint_names", {"base_mount_to_upper_base", "upper_base_to_shoulder", "shoulder_to_elbow",
                        "elbow_to_wrist", "wrist_to_claw"});

    frame_ = declare_parameter("command_frame", "base_mount");

    enable_button_ = declare_parameter("enable_button", 9);
    mode_button_ = declare_parameter("mode_button", 6);

    axes_ = declare_parameter<std::vector<int64_t>>("axes", {0, 1, 2, 3});

    signs_ = declare_parameter<std::vector<double>>("axis_signs", {1.0, -1.0, 1.0, -1.0});

    wrist_negative_ = declare_parameter("wrist_negative_button", 13);
    wrist_positive_ = declare_parameter("wrist_positive_button", 14);
    yaw_positive_ = declare_parameter("yaw_positive_button", 11);
    yaw_negative_ = declare_parameter("yaw_negative_button", 12);

    gripper_open_axis_ = declare_parameter("gripper_open_axis", 4);
    gripper_close_axis_ = declare_parameter("gripper_close_axis", 5);
    gripper_trigger_threshold_ = declare_parameter("gripper_trigger_threshold", 0.5);

    gripper_open_position_ = declare_parameter("gripper_open_position", 0.0);
    gripper_closed_position_ = declare_parameter("gripper_closed_position", 0.05);
    gripper_speed_ = declare_parameter("gripper_speed", 0.02);

    deadzone_ = declare_parameter("deadzone", 0.1);
    timeout_ = declare_parameter("joy_timeout", 0.5);
    linear_speed_ = declare_parameter("linear_speed", 0.15);
    angular_speed_ = declare_parameter("angular_speed", 0.3);
    joint_speed_ = declare_parameter("joint_speed", 0.5);

    if (joint_names_.size() != 5 || (axes_.size() != 4 && axes_.size() != 5) ||
        signs_.size() != axes_.size() || !std::isfinite(deadzone_) || deadzone_ < 0.0 ||
        deadzone_ >= 1.0 || !std::isfinite(timeout_) || timeout_ <= 0.0 ||
        !std::isfinite(linear_speed_) || linear_speed_ <= 0.0 || !std::isfinite(angular_speed_) ||
        angular_speed_ <= 0.0 || !std::isfinite(joint_speed_) || joint_speed_ <= 0.0) {
      throw std::runtime_error("Invalid arm teleop configuration");
    }

    if (gripper_open_axis_ < 0 || gripper_close_axis_ < 0 ||
        gripper_open_axis_ == gripper_close_axis_ || !std::isfinite(gripper_trigger_threshold_) ||
        gripper_trigger_threshold_ <= 0.0 || gripper_trigger_threshold_ >= 1.0 ||
        !std::isfinite(gripper_open_position_) || !std::isfinite(gripper_closed_position_) ||
        gripper_open_position_ < 0.0 || gripper_closed_position_ > 0.05 ||
        gripper_open_position_ >= gripper_closed_position_ || !std::isfinite(gripper_speed_) ||
        gripper_speed_ <= 0.0 || gripper_speed_ > 0.02) {
      throw std::runtime_error("Invalid gripper configuration");
    }

    for (double sign : signs_) {
      if (sign != 1.0 && sign != -1.0) {
        throw std::runtime_error("axis_signs must contain only 1.0 or -1.0");
      }
    }

    twist_publisher_ =
        create_publisher<geometry_msgs::msg::TwistStamped>("/arm_teleop/delta_twist_cmds", 10);

    joint_publisher_ =
        create_publisher<control_msgs::msg::JointJog>("/arm_teleop/delta_joint_cmds", 10);

    mode_client_ = create_client<ServoCommand>("/servo_node/switch_command_type");

    gripper_publisher_ =
        create_publisher<std_msgs::msg::Float64MultiArray>("/gripper_controller/commands", 1);
    joint_state_subscription_ = create_subscription<sensor_msgs::msg::JointState>(
        "/joint_states", rclcpp::SensorDataQoS(),
        [this](sensor_msgs::msg::JointState::ConstSharedPtr msg) { read_gripper_state(*msg); });

    joy_subscription_ = create_subscription<sensor_msgs::msg::Joy>(
        "/joy", rclcpp::SensorDataQoS(),
        [this](sensor_msgs::msg::Joy::ConstSharedPtr msg) { read_joystick(*msg); });

    timer_ = create_wall_timer(20ms, [this]() { update(); });

    RCLCPP_INFO(get_logger(), "Starting in joint mode. Release enable at neutral before moving.");

    RCLCPP_INFO(get_logger(), "Gripper controls: L2 axis %d opens, R2 axis %d closes.",
                gripper_open_axis_, gripper_close_axis_);
  }

private:
  std::vector<std::string> joint_names_;
  std::string frame_;

  std::vector<int64_t> axes_;
  std::vector<double> signs_;

  int enable_button_;
  int mode_button_;
  int wrist_negative_;
  int wrist_positive_;
  int yaw_positive_;
  int yaw_negative_;

  int gripper_open_axis_;
  int gripper_close_axis_;

  double gripper_trigger_threshold_;
  double gripper_open_position_;
  double gripper_closed_position_;
  double gripper_speed_;
  int gripper_direction_ = 0;
  const std::array<std::string, 2> gripper_joints_{"claw_to_left_grip", "claw_to_right_grip"};
  std::array<double, 2> gripper_positions_{};
  bool have_gripper_state_ = false;
  Clock::time_point last_gripper_state_{};

  double deadzone_;
  double timeout_;
  double linear_speed_;
  double angular_speed_;
  double joint_speed_;

  std::array<double, 5> inputs_{};
  double yaw_ = 0.0;

  bool have_input_ = false;
  bool enable_ = false;
  bool armed_ = false;

  bool cartesian_ = false;
  bool previous_mode_button_ = false;

  bool mode_ready_ = false;
  bool mode_pending_ = false;

  int64_t mode_request_id_ = 0;

  Clock::time_point last_input_{};
  Clock::time_point next_mode_attempt_{};
  Clock::time_point mode_request_time_{};

  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_subscription_;

  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr twist_publisher_;

  rclcpp::Publisher<control_msgs::msg::JointJog>::SharedPtr joint_publisher_;

  rclcpp::Client<ServoCommand>::SharedPtr mode_client_;

  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr gripper_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_state_subscription_;

  rclcpp::TimerBase::SharedPtr timer_;

  bool valid_input(const sensor_msgs::msg::Joy &joy) const
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

    if (gripper_open_axis_ < 0 || gripper_close_axis_ < 0 ||
        static_cast<size_t>(gripper_open_axis_) >= joy.axes.size() ||
        static_cast<size_t>(gripper_close_axis_) >= joy.axes.size() ||
        !std::isfinite(joy.axes[gripper_open_axis_]) ||
        !std::isfinite(joy.axes[gripper_close_axis_])) {
      return false;
    }

    return true;
  }

  void read_gripper_state(const sensor_msgs::msg::JointState &state)
  {
    have_gripper_state_ = false;
    for (size_t i = 0; i < gripper_joints_.size(); ++i) {
      const auto joint = std::find(state.name.begin(), state.name.end(), gripper_joints_[i]);
      const auto index = static_cast<size_t>(joint - state.name.begin());
      if (joint == state.name.end() || index >= state.position.size() ||
          !std::isfinite(state.position[index])) {
        return;
      }
      gripper_positions_[i] = state.position[index];
    }
    last_gripper_state_ = Clock::now();
    have_gripper_state_ = true;
  }

  void publish_gripper_command(bool fresh_input, Clock::time_point time)
  {
    std_msgs::msg::Float64MultiArray command;
    command.data = {0.0, 0.0};
    const bool fresh_state =
        have_gripper_state_ &&
        std::chrono::duration<double>(time - last_gripper_state_).count() <= timeout_;
    if (fresh_input && fresh_state) {
      for (size_t i = 0; i < gripper_positions_.size(); ++i) {
        // A small tolerance avoids commanding into a numerically rounded hard stop.
        const bool at_limit = gripper_direction_ > 0
                                  ? gripper_positions_[i] >= gripper_closed_position_ - 1e-5
                                  : gripper_positions_[i] <= gripper_open_position_ + 1e-5;
        if (!at_limit) {
          command.data[i] = gripper_direction_ * gripper_speed_;
        }
      }
    }
    gripper_publisher_->publish(command);
  }

  void read_joystick(const sensor_msgs::msg::Joy &joy)
  {
    if (!valid_input(joy)) {
      have_input_ = false;
      armed_ = false;

      gripper_direction_ = 0;

      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                           "Invalid /joy message. Check controller indices.");

      return;
    }

    const auto received = Clock::now();

    if (std::chrono::duration<double>(received - last_input_).count() > timeout_) {
      armed_ = false;
    }

    last_input_ = received;
    have_input_ = true;

    enable_ = joy.buttons[enable_button_] != 0;

    // PS5 analog triggers:
    // Released = +1.0
    // Fully pressed = -1.0

    const bool open = joy.axes[gripper_open_axis_] < -gripper_trigger_threshold_;

    const bool close = joy.axes[gripper_close_axis_] < -gripper_trigger_threshold_;

    // Hold either trigger for constant speed; both pressed (or released) stops.
    gripper_direction_ = static_cast<int>(close) - static_cast<int>(open);

    // Preserve existing arm joystick mapping.

    for (size_t i = 0; i < axes_.size(); ++i) {
      const double value = std::clamp(static_cast<double>(joy.axes[axes_[i]]), -1.0, 1.0);

      inputs_[i] = std::abs(value) <= deadzone_ ? 0.0 : signs_[i] * value;
    }

    if (axes_.size() == 4) {
      inputs_[4] = static_cast<double>((joy.buttons[wrist_positive_] != 0) -
                                       (joy.buttons[wrist_negative_] != 0));
    }

    yaw_ =
        static_cast<double>((joy.buttons[yaw_positive_] != 0) - (joy.buttons[yaw_negative_] != 0));

    const bool neutral =
        std::all_of(inputs_.begin(), inputs_.end(), [](double value) { return value == 0.0; }) &&
        yaw_ == 0.0;

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

    request->command_type =
        cartesian_ ? ServoCommand::Request::TWIST : ServoCommand::Request::JOINT_JOG;

    mode_pending_ = true;
    mode_request_time_ = Clock::now();

    auto pending = mode_client_->async_send_request(
        request, [this](rclcpp::Client<ServoCommand>::SharedFuture response) {
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
    const bool fresh =
        have_input_ && std::chrono::duration<double>(time - last_input_).count() <= timeout_;

    // The gripper is independent of the arm's enable button and Servo mode.
    publish_gripper_command(fresh, time);
    if (!fresh) {
      armed_ = false;
    }

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
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);

  rclcpp::spin(std::make_shared<ArmTeleop>());

  rclcpp::shutdown();

  return 0;
}
