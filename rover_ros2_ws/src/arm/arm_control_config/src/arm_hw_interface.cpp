#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <embedded/devices/ODriveDevice.hpp>
#include <hardware_interface/system_interface.hpp>
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/state.hpp>

namespace arm_control_config
{
using CallbackReturn = hardware_interface::CallbackReturn;
using ReturnType = hardware_interface::return_type;
using namespace std::chrono_literals;

class ArmHardwareInterface : public hardware_interface::SystemInterface
{
public:
  CallbackReturn on_init(const hardware_interface::HardwareInfo & info) override
  {
    if (SystemInterface::on_init(info) != CallbackReturn::SUCCESS) {
      return CallbackReturn::ERROR;
    }

    try {
      can_interface_ = info_.hardware_parameters.at("can_interface");
      if (can_interface_.empty() || info_.joints.empty()) {
        throw std::runtime_error("Specify a CAN interface and at least one ODrive joint.");
      }

      std::set<unsigned int> ids;
      for (const auto & joint : info_.joints) {
        if (joint.command_interfaces.size() != 1 ||
            (joint.command_interfaces[0].name != "position" &&
             joint.command_interfaces[0].name != "velocity")) {
          throw std::runtime_error(joint.name + ": require one position or velocity command interface.");
        }
        std::set<std::string> states;
        for (const auto & interface : joint.state_interfaces) {
          states.insert(interface.name);
        }
        if (joint.state_interfaces.size() != 2 ||
            states != std::set<std::string>{"position", "velocity"}) {
          throw std::runtime_error(joint.name + ": require position and velocity state interfaces.");
        }

        const auto number = [&](const char * key) {
          const auto & value = joint.parameters.at(key);
          std::size_t used = 0;
          const double result = std::stod(value, &used);
          if (used != value.size() || !std::isfinite(result)) {
            throw std::runtime_error(joint.name + ": invalid " + key);
          }
          return result;
        };

        const double id = number("can_id");
        if (id < 0 || id > 62 || std::floor(id) != id ||
            !ids.insert(static_cast<unsigned int>(id)).second) {
          throw std::runtime_error(joint.name + ": CAN ID must be unique and between 0 and 62.");
        }

        Joint data;
        data.name = joint.name;
        data.id = static_cast<uint8_t>(id);
        data.velocity_command = joint.command_interfaces[0].name == "velocity";
        data.scale = number("revolutions_per_unit");
        data.zero = number("zero_revolutions");
        if (data.scale == 0.0) {
          throw std::runtime_error(joint.name + ": revolutions_per_unit cannot be zero.");
        }
        joints_.push_back(data);
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(rclcpp::get_logger("arm_hw_interface"), "%s", error.what());
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::SUCCESS;
  }

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override
  {
    std::vector<hardware_interface::StateInterface> interfaces;
    for (auto & joint : joints_) {
      interfaces.emplace_back(joint.name, "position", &joint.position);
      interfaces.emplace_back(joint.name, "velocity", &joint.velocity);
    }
    return interfaces;
  }

  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override
  {
    std::vector<hardware_interface::CommandInterface> interfaces;
    for (auto & joint : joints_) {
      interfaces.emplace_back(
        joint.name, joint.velocity_command ? "velocity" : "position", &joint.command);
    }
    return interfaces;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Drives must already be calibrated, configured, and broadcasting telemetry.
    // Registration obtains the shared CAN manager automatically.
    try {
      for (auto & joint : joints_) {
        joint.motor = ODriveDevice::register_device(
          joint.name, joint.id, 500ms, 100ms, can_interface_);
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(rclcpp::get_logger("arm_hw_interface"), "%s", error.what());
      release_devices();
      return CallbackReturn::ERROR;
    }

    if (!set_axis_state(ODriveDevice::kAxisStateIdle)) {
      release_devices();
      return CallbackReturn::ERROR;
    }
    configured_ = true;
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    if (!configured_ || read(rclcpp::Time(0), rclcpp::Duration(0, 0)) != ReturnType::OK) {
      return CallbackReturn::ERROR;
    }
    for (auto & joint : joints_) {
      joint.command = joint.velocity_command ? 0.0 : joint.position;
    }
    if (!send_commands() || !set_axis_state(ODriveDevice::kAxisStateClosedLoop)) {
      set_axis_state(ODriveDevice::kAxisStateIdle);
      return CallbackReturn::ERROR;
    }
    active_ = true;
    return CallbackReturn::SUCCESS;
  }

  ReturnType read(const rclcpp::Time &, const rclcpp::Duration &) override
  {
    if (!configured_) {
      return ReturnType::OK;
    }
    for (auto & joint : joints_) {
      if (!joint.motor->is_healthy() || joint.motor->get_axis_error() != 0 ||
          (active_ && joint.motor->get_axis_state() != ODriveDevice::kAxisStateClosedLoop)) {
        return ReturnType::ERROR;
      }

      // Cached telemetry: this API does not expose encoder freshness timestamps.
      joint.position = (joint.motor->get_pos() - joint.zero) / joint.scale;
      joint.velocity = joint.motor->get_vel() / joint.scale;
      if (!std::isfinite(joint.position) || !std::isfinite(joint.velocity)) {
        return ReturnType::ERROR;
      }
    }
    return ReturnType::OK;
  }

  ReturnType write(const rclcpp::Time &, const rclcpp::Duration &) override
  {
    if (!active_) {
      return ReturnType::OK;
    }
    return send_commands() ? ReturnType::OK : ReturnType::ERROR;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override {
    active_ = false;
    return set_axis_state(ODriveDevice::kAxisStateIdle) ?
      CallbackReturn::SUCCESS : CallbackReturn::ERROR;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override {
    return release_devices();
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override{
    return release_devices();
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State &) override{
    return release_devices();
  }

private:
  struct Joint
  {
    std::string name;
    uint8_t id = 0;
    bool velocity_command = false;
    // Motor revolutions = joint position * scale + zero.
    // Use rev/rad for revolute joints or rev/m for prismatic joints.
    // A negative scale reverses the joint direction.
    double scale = 1.0;
    double zero = 0.0;
    double position = std::numeric_limits<double>::quiet_NaN();
    double velocity = std::numeric_limits<double>::quiet_NaN();
    double command = std::numeric_limits<double>::quiet_NaN();
    std::shared_ptr<ODriveDevice> motor;
  };

  bool send_commands()
  {
    // Validate every target before updating any device.
    for (const auto & joint : joints_) {
      const double target = joint.command * joint.scale + (joint.velocity_command ? 0.0 : joint.zero);
      if (!joint.motor || !joint.motor->is_healthy() || joint.motor->get_axis_error() != 0 || !std::isfinite(target) || std::abs(target) > std::numeric_limits<float>::max()) {
        return false;
      }
    }
    for (const auto & joint : joints_) {
      if (joint.velocity_command) {
        joint.motor->set_vel(static_cast<float>(joint.command * joint.scale));
      } else {
        joint.motor->set_pos(static_cast<float>(joint.command * joint.scale + joint.zero));
      }
    }
    return true;
  }

  bool set_axis_state(uint32_t state)
  {
    bool sent = true;
    for (const auto & joint : joints_) {
      if (joint.motor && !joint.motor->set_axis(state)) {
        sent = false;
      }
    }
    if (!sent) {
      return false;
    }

    // Only lifecycle callbacks wait; read() and write() never wait for CAN replies.
    const auto deadline = std::chrono::steady_clock::now() + 7s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (state == ODriveDevice::kAxisStateClosedLoop && !send_commands()) {
        return false;
      }
      bool acknowledged = true;
      for (const auto & joint : joints_) {
        if (joint.motor &&
            (joint.motor->axis_service_state() != ServiceState::RECEIVED ||
             joint.motor->get_axis_state() != state)) {
          acknowledged = false;
        }
      }
      if (acknowledged) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    RCLCPP_ERROR(rclcpp::get_logger("arm_hw_interface"), "ODrive axis-state request timed out.");
    return false;
  }

  CallbackReturn release_devices()
  {
    active_ = false;
    const bool stopped = set_axis_state(ODriveDevice::kAxisStateIdle);
    configured_ = false;
    for (auto & joint : joints_) {
      joint.motor.reset();
    }
    return stopped ? CallbackReturn::SUCCESS : CallbackReturn::ERROR;
  }

  std::string can_interface_;
  std::vector<Joint> joints_;
  bool configured_ = false;
  bool active_ = false;
};
}  // namespace arm_control_config

PLUGINLIB_EXPORT_CLASS(arm_control_config::ArmHardwareInterface, hardware_interface::SystemInterface)