#include "embedded/devices/YellowJacketDevice.hpp"

#include <cstring>
#include <utility>

YellowJacketDevice::YellowJacketDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    std::string manager_name)
    : CanDeviceFactory<YellowJacketDevice>(
          name,
          CanFilter::can_simple(node_id),
          rx_timeout,
          control_timeout,
          std::move(manager_name),
          node_id),
      node_id_(node_id),
      setpoint_periodic_(create_periodic(
          std::chrono::microseconds(20000),
          [this]() { return build_normal_setpoint_frame(); },
          [this]() { return build_safe_setpoint_frame(); })),
      set_controller_mode_cmd_(create_immediate([this]() -> std::optional<CanFrame> {
        CanFrame frame = CanFrame::make_can_simple(node_id_, kCmdSetControllerMode, 8);
        const uint8_t control_mode_bytes[8] = {
            0x05,
            0x00,
            0x00,
            0x00,
            static_cast<uint8_t>(control_mode_.load(std::memory_order_acquire)),
            0x00,
            0x00,
            0x00};
        std::memcpy(frame.data.data(), control_mode_bytes, sizeof(control_mode_bytes));
        return frame;
      })) {}

void YellowJacketDevice::set_pos(float pos_rev) noexcept {
  target_pos_.store(pos_rev, std::memory_order_release);
  control_mode_.store(ControlMode::POSITION, std::memory_order_release);
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
  if (control_mode_.load(std::memory_order_acquire) != ControlMode::DUTY_CYCLE) {
    control_mode_.store(ControlMode::DUTY_CYCLE, std::memory_order_release);
    set_controller_mode();
  }
  mark_control_updated();
}

bool YellowJacketDevice::set_controller_mode() noexcept {
  return set_controller_mode_cmd_.send();
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
      int32_t duty = target_duty_.load(std::memory_order_acquire);
      std::memcpy(&f.data[0], &duty, sizeof(int32_t));
      return f;
    }
  }
  return std::nullopt;
}

std::optional<CanFrame> YellowJacketDevice::build_safe_setpoint_frame() const noexcept {
  // Degraded SOFT_BLOCK behavior when control commands go stale: command 0 velocity & 0 torque FF
  return CanFrame::make_can_simple(node_id_, kCmdSetInputVelandDuty, 8);
}