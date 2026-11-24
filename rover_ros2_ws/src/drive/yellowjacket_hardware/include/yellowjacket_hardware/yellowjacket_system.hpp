#ifndef YELLOWJACKET_HARDWARE__YELLOWJACKET_SYSTEM_HPP_
#define YELLOWJACKET_HARDWARE__YELLOWJACKET_SYSTEM_HPP_

#include <chrono>
#include <string>
#include <vector>

#include "hardware_interface/system_interface.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/state.hpp"
#include <memory>
#include "embedded/core/CanManager.hpp"
#include "embedded/devices/YellowJacketDevice.hpp"

namespace yellowjacket_hardware
{

class YellowjacketSystem : public hardware_interface::SystemInterface
{
public:
  // This Jazzy-compatible API exports interfaces backed by our own vectors.
  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareInfo & info) override;
  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  hardware_interface::CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

  hardware_interface::return_type read(
    const rclcpp::Time &, const rclcpp::Duration &) override;
  hardware_interface::return_type write(
    const rclcpp::Time &, const rclcpp::Duration &) override;

private:
  // Transport boundaries: implement these once the CAN framing is specified.
  bool open_transport();
  bool stop_motors();
  void close_transport();
  bool connected_{false};
  std::shared_ptr<CanManager> can_manager_;
  std::vector<std::shared_ptr<YellowJacketDevice>> motors_;
  std::string can_interface_{"can0"};
  // Never resize these after exporting pointers into them.
  std::vector<double> commands_;    // Wheel rad/s.
  std::vector<double> positions_;   // Wheel radians, from encoder feedback.
  std::vector<double> velocities_;  // Wheel rad/s, from encoder feedback.
  // Log every motor once per second, independently for read and write.
  std::chrono::steady_clock::time_point next_read_log_{};
  std::chrono::steady_clock::time_point next_write_log_{};
};

}  // namespace yellowjacket_hardware
#endif
