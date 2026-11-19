#include <limits>
#include <memory>

#include "gtest/gtest.h"
#include "pluginlib/class_loader.hpp"
#include "yellowjacket_hardware/encoder_units.hpp"
#include "yellowjacket_hardware/yellowjacket_system.hpp"

namespace
{
hardware_interface::HardwareInfo wheel_info()
{
  hardware_interface::HardwareInfo info;
  info.name = "BenchMotor";
  info.type = "system";
  hardware_interface::ComponentInfo joint;
  joint.name = "wheel_joint";
  hardware_interface::InterfaceInfo velocity;
  velocity.name = "velocity";
  hardware_interface::InterfaceInfo position;
  position.name = "position";
  joint.command_interfaces = {velocity};
  joint.state_interfaces = {position, velocity};
  info.joints.push_back(joint);
  return info;
}

TEST(Plugin, LoadsExportsInterfacesAndRefusesUnimplementedTransport)
{
  rclcpp::init(0, nullptr);
  {
    pluginlib::ClassLoader<hardware_interface::SystemInterface> loader(
      "hardware_interface", "hardware_interface::SystemInterface");
    auto plugin = loader.createSharedInstance("yellowjacket_hardware/YellowjacketSystem");
    ASSERT_EQ(plugin->on_init(wheel_info()), hardware_interface::CallbackReturn::SUCCESS);
    const auto commands = plugin->export_command_interfaces();
    const auto states = plugin->export_state_interfaces();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].get_name(), "wheel_joint/velocity");
    ASSERT_EQ(states.size(), 2u);
    EXPECT_EQ(states[0].get_name(), "wheel_joint/position");
    EXPECT_EQ(states[1].get_name(), "wheel_joint/velocity");
    const rclcpp_lifecycle::State state;
    EXPECT_EQ(plugin->on_configure(state), hardware_interface::CallbackReturn::FAILURE);
    EXPECT_EQ(plugin->on_activate(state), hardware_interface::CallbackReturn::FAILURE);
    EXPECT_EQ(plugin->read(rclcpp::Time(0), rclcpp::Duration(0, 0)),
      hardware_interface::return_type::ERROR);
    EXPECT_EQ(plugin->write(rclcpp::Time(0), rclcpp::Duration(0, 0)),
      hardware_interface::return_type::ERROR);
  }
  rclcpp::shutdown();
}

TEST(Plugin, RejectsWrongCommandAndDuplicateJoint)
{
  rclcpp::init(0, nullptr);
  {
    auto wrong = wheel_info();
    wrong.joints[0].command_interfaces[0].name = "effort";
    yellowjacket_hardware::YellowjacketSystem plugin;
    EXPECT_EQ(plugin.on_init(wrong), hardware_interface::CallbackReturn::ERROR);
    auto duplicate = wheel_info();
    duplicate.joints.push_back(duplicate.joints.front());
    yellowjacket_hardware::YellowjacketSystem another;
    EXPECT_EQ(another.on_init(duplicate), hardware_interface::CallbackReturn::ERROR);
  }
  rclcpp::shutdown();
}

TEST(Units, ConvertsBothDirectionsAndRejectsInvalidScale)
{
  // Synthetic calibration, not the actual Yellow Jacket encoder specification.
  const yellowjacket_hardware::EncoderUnits units(1000.0);
  EXPECT_NEAR(units.to_radians(500.0), 3.141592653589793, 1e-12);
  EXPECT_NEAR(units.to_counts(-6.283185307179586), -1000.0, 1e-10);
  EXPECT_NEAR(units.to_radians(units.to_counts(2.5)), 2.5, 1e-12);
  EXPECT_THROW(yellowjacket_hardware::EncoderUnits(0.0), std::invalid_argument);
  EXPECT_THROW(yellowjacket_hardware::EncoderUnits(-1.0), std::invalid_argument);
  EXPECT_THROW(yellowjacket_hardware::EncoderUnits(
    std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}
}  // namespace
