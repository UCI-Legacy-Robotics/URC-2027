#include "yellowjacket_hardware/yellowjacket_system.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "pluginlib/class_list_macros.hpp"
#include <memory>
#include "embedded/core/CanManager.hpp"
#include "embedded/devices/YellowJacketDevice.hpp"

constexpr double TWO_PI = 6.283185307179586;

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
  for (std::size_t i =0; i<motors_.size() ; i++){
    positions_[i] = motors_[i]->get_pos() * TWO_PI;
    velocities_[i] = motors_[i]->get_vel() * TWO_PI;    
  }
  return return_type::OK;
}

return_type YellowjacketSystem::write(const rclcpp::Time &, const rclcpp::Duration &)
{
  for (std::size_t i=0; i<motors_.size(); i++){
    motors_[i]->set_duty((int)(commands_[i] / 5.3 * 32700));
  }
  return return_type::OK;
}

bool YellowjacketSystem::open_transport()
{
  // Register the six motors using default timeouts (200 ms RX, 100 ms control)
  // and the default CAN interface, can0.
  
  motors_.push_back(YellowJacketDevice::register_device("left_front_wheel_joint", 4));
  motors_.push_back(YellowJacketDevice::register_device("right_front_wheel_joint", 5));
  motors_.push_back(YellowJacketDevice::register_device("left_middle_wheel_joint", 6));
  motors_.push_back(YellowJacketDevice::register_device("right_middle_wheel_joint", 7));
  motors_.push_back(YellowJacketDevice::register_device("left_back_wheel_joint", 8));
  motors_.push_back(YellowJacketDevice::register_device("right_back_wheel_joint", 9));

  
  return true;
}

bool YellowjacketSystem::stop_motors()
{
  for (std::size_t i=0; i<motors_.size(); i++){
    motors_[i]->set_duty(0);
  }
  return true;
}

void YellowjacketSystem::close_transport()
{
  motors_.clear();
  connected_ = false;
}

}  // namespace yellowjacket_hardware

PLUGINLIB_EXPORT_CLASS(
  yellowjacket_hardware::YellowjacketSystem, hardware_interface::SystemInterface)
