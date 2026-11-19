#include "embedded/devices/YellowJacketDevice.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

std::shared_ptr<YellowJacketDevice> YellowJacketDevice::register_device(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    const std::string& manager_name) {
  auto device = std::shared_ptr<YellowJacketDevice>(
      new YellowJacketDevice(name, node_id, rx_timeout, control_timeout, manager_name));
  CanDevice::register_with_manager(device);
  return device;
}

std::shared_ptr<YellowJacketDevice> YellowJacketDevice::find_device(
    std::optional<std::string> name,
    std::optional<uint32_t> id,
    const std::string& manager_name) {
  auto device = CanDevice::find_registered_device(name, id, manager_name);
  auto controller = std::dynamic_pointer_cast<YellowJacketDevice>(device);
  if (!controller) {
    if (name && !name->empty()) {
      throw std::runtime_error("CAN device named '" + *name + "' is not a YellowJacket");
    }
    throw std::runtime_error(
        "CAN device with ID " + std::to_string(*id) + " is not a YellowJacket");
  }
  return controller;
}

YellowJacketDevice::YellowJacketDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    std::string manager_name)
    : CanDevice(
          name,
          CanFilter::can_simple(node_id),
          rx_timeout,
          control_timeout,
          std::move(manager_name),
          node_id),
      node_id_(node_id),
      setpoint_periodic_(create_periodic(
          std::chrono::microseconds(1000000),  // BS test val
          [this]() { return build_normal_setpoint_frame(); },
          [this]() { return build_safe_setpoint_frame(); })),
      set_controller_mode_cmd_(*this, CanFrame::make_can_simple(node_id, kCmdSetControllerMode, 8)) {}

void YellowJacketDevice::set_pos(float pos_rev) noexcept {
  target_pos_.store(pos_rev, std::memory_order_release);
  control_mode_.store(ControlMode::POSITION, std::memory_order_release);
  set_controller_mode_cmd_.send();
  set_controller_mode();
  mark_control_updated();
}

void YellowJacketDevice::set_vel(float vel_rev_s) noexcept {
  target_vel_.store(vel_rev_s, std::memory_order_release);
  control_mode_.store(ControlMode::VELOCITY, std::memory_order_release);
  set_controller_mode();
  mark_control_updated();
}

void YellowJacketDevice::set_duty(int32_t duty_pwm) noexcept {
  // -32768 to 32767 range corresponding to -100% to 100% duty cycle
  target_duty_.store(duty_pwm, std::memory_order_release);
  control_mode_.store(ControlMode::DUTY_CYCLE, std::memory_order_release);
  set_controller_mode();
  mark_control_updated();
}

bool YellowJacketDevice::set_controller_mode() noexcept {
  CanFrame frame = CanFrame::make_can_simple(node_id_, kCmdSetControllerMode, 8);
  uint8_t control_mode_bytes[8] = {0x00, 0x00, 0x00, 0x05, 0x00, static_cast<uint8_t>(control_mode_.load(std::memory_order_acquire)), 0x00, 0x00};
  std::memcpy(frame.data.data(), control_mode_bytes, sizeof(control_mode_bytes));
  return set_controller_mode_cmd_.send(frame);
}

void YellowJacketDevice::on_rx(const CanFrame& frame) {
  switch (frame.can_simple_cmd_id()) {
    case kCmdHeartbeat:
      break;
    case kCmdGetEncoderEstimates:
      break;
    default:
      break;
  }
}

std::optional<CanFrame> YellowJacketDevice::build_normal_setpoint_frame() const noexcept {
  switch (control_mode_.load(std::memory_order_acquire)) {
    case ControlMode::POSITION: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputPos, 8);
      float pos = target_pos_.load(std::memory_order_acquire);
      // std::memcpy(&f.data[0], &target_pos_.load(std::memory_order_acquire), sizeof(int32_t));
      return f;
    }

    case ControlMode::VELOCITY: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVelandDuty, 8);
      // std::memcpy(&f.data[0], &target_vel_.load(std::memory_order_acquire), sizeof(int32_t));
      return f;
    }

    case ControlMode::DUTY_CYCLE: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVelandDuty, 8);
      // std::memcpy(&f.data[0], &target_duty_.load(std::memory_order_acquire), sizeof(int32_t));
      return f;
    }
  }
  return std::nullopt;
}

std::optional<CanFrame> YellowJacketDevice::build_safe_setpoint_frame() const noexcept {
  // Degraded SOFT_BLOCK behavior when control commands go stale: command 0 velocity & 0 torque FF
  return CanFrame::make_can_simple(node_id_, kCmdSetInputVelandDuty, 8);
}