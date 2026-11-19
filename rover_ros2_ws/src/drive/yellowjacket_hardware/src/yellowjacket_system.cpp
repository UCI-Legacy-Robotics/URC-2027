#include "yellowjacket_hardware/yellowjacket_system.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "pluginlib/class_list_macros.hpp"

namespace yellowjacket_hardware
{
using hardware_interface::CallbackReturn;
using hardware_interface::return_type;

CallbackReturn YellowjacketSystem::on_init(const hardware_interface::HardwareInfo & info)
{
  if (SystemInterface::on_init(info) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }
  std::unordered_set<std::string> names;
  if (info_.joints.empty()) {
    return CallbackReturn::ERROR;
  }
  for (const auto & joint : info_.joints) {
    // Each wheel accepts velocity and reports position plus velocity.
    if (!names.insert(joint.name).second || joint.command_interfaces.size() != 1 ||
      joint.command_interfaces.front().name != hardware_interface::HW_IF_VELOCITY ||
      joint.state_interfaces.size() != 2)
    {
      RCLCPP_ERROR(get_logger(), "Invalid interfaces or duplicate joint: %s", joint.name.c_str());
      return CallbackReturn::ERROR;
    }
    std::unordered_set<std::string> states;
    for (const auto & state : joint.state_interfaces) {
      states.insert(state.name);
    }
    if (states.count(hardware_interface::HW_IF_POSITION) != 1 ||
      states.count(hardware_interface::HW_IF_VELOCITY) != 1)
    {
      return CallbackReturn::ERROR;
    }
  }
  const auto interface = info_.hardware_parameters.find("can_interface");
  if (interface != info_.hardware_parameters.end()) {
    can_interface_ = interface->second;
  }
  if (can_interface_.empty()) {
    return CallbackReturn::ERROR;
  }
  commands_.assign(info_.joints.size(), 0.0);
  // Unknown measurements must not be presented as successful zero feedback.
  positions_.assign(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  velocities_.assign(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  return CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::CommandInterface> YellowjacketSystem::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> interfaces;
  for (std::size_t i = 0; i < info_.joints.size(); ++i) {
    interfaces.emplace_back(info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &commands_[i]);
  }
  return interfaces;
}

std::vector<hardware_interface::StateInterface> YellowjacketSystem::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> interfaces;
  for (std::size_t i = 0; i < info_.joints.size(); ++i) {
    interfaces.emplace_back(info_.joints[i].name, hardware_interface::HW_IF_POSITION, &positions_[i]);
    interfaces.emplace_back(info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &velocities_[i]);
  }
  return interfaces;
}

CallbackReturn YellowjacketSystem::on_configure(const rclcpp_lifecycle::State &)
{
  connected_ = open_transport();
  return connected_ ? CallbackReturn::SUCCESS : CallbackReturn::FAILURE;
}

CallbackReturn YellowjacketSystem::on_activate(const rclcpp_lifecycle::State &)
{
  std::fill(commands_.begin(), commands_.end(), 0.0);
  // Activation requires a confirmed stop and valid initial encoder feedback.
  if (!connected_ || !stop_motors() ||
    read(rclcpp::Time(0), rclcpp::Duration(0, 0)) != return_type::OK)
  {
    return CallbackReturn::FAILURE;
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn YellowjacketSystem::on_deactivate(const rclcpp_lifecycle::State &)
{
  std::fill(commands_.begin(), commands_.end(), 0.0);
  return connected_ && stop_motors() ? CallbackReturn::SUCCESS : CallbackReturn::ERROR;
}

CallbackReturn YellowjacketSystem::on_cleanup(const rclcpp_lifecycle::State &)
{
  const bool stopped = !connected_ || stop_motors();
  close_transport();
  return stopped ? CallbackReturn::SUCCESS : CallbackReturn::ERROR;
}

CallbackReturn YellowjacketSystem::on_shutdown(const rclcpp_lifecycle::State & state)
{
  return on_cleanup(state);
}

CallbackReturn YellowjacketSystem::on_error(const rclcpp_lifecycle::State & state)
{
  return on_cleanup(state);
}

return_type YellowjacketSystem::read(const rclcpp::Time &, const rclcpp::Duration &)
{
  // TODO: Request/collect bounded-time encoder transactions over the CAN bridge.
  // Validate complete replies, CRC, node/channel, freshness, sign and encoder rollover.
  // With N decoded counts per wheel revolution:
  // positions_[i] = accumulated_counts * 2*pi / N;
  // velocities_[i] = measured_counts_per_second * 2*pi / N;
  return return_type::ERROR;
}

return_type YellowjacketSystem::write(const rclcpp::Time &, const rclcpp::Duration &)
{
  // TODO: Check finite targets and configured speed limits, then convert:
  // target_counts_per_second = commands_[i] * N / (2*pi).
  // Range-check before integer conversion; serialize RoboClaw speed packets plus CRC,
  // fragment into CAN frames as specified by the PCB, and validate acknowledgements.
  return return_type::ERROR;
}

bool YellowjacketSystem::open_transport()
{
  // TODO: Validate per-joint mapping and scaling, open CAN, and check RoboClaw replies.
  // No placeholder CAN IDs are transmitted by this scaffold.
  RCLCPP_ERROR(get_logger(),
    "CAN transport on '%s' is not implemented; hardware configuration refused.",
    can_interface_.c_str());
  return false;
}

bool YellowjacketSystem::stop_motors()
{
  // TODO: Send actual zero-speed commands and verify the result with a bounded timeout.
  return false;
}

void YellowjacketSystem::close_transport()
{
  // TODO: Release the CAN socket when one is implemented.
  connected_ = false;
}

}  // namespace yellowjacket_hardware

PLUGINLIB_EXPORT_CLASS(
  yellowjacket_hardware::YellowjacketSystem, hardware_interface::SystemInterface)
