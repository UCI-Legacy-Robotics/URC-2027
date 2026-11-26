#include "embedded/devices/StepperDevice.hpp"

#include <cstring>
#include <utility>

// --- Construction ---
StepperDevice::StepperDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    std::string manager_name)
    : CanDeviceFactory<StepperDevice>(
          name,
          CanFilter::can_simple(node_id),
          rx_timeout,
          control_timeout,
          std::move(manager_name),
          node_id),
      node_id_(node_id),
      setpoint_periodic_(create_periodic(
          std::chrono::microseconds(20000),  // 50 Hz
          [this]() { return build_normal_setpoint_frame(); },
          [this]() { return build_safe_setpoint_frame(); })),
      set_controller_mode_cmd_(create_immediate([this]() { return build_controller_mode_frame(); })) {}

// --- Setpoints ---
// stores the target; sends the mode only when switching into position mode
bool StepperDevice::set_pos(int32_t pos_steps) noexcept {
  bool success = true;
  target_pos_.store(pos_steps, std::memory_order_release);

  ControlMode old = control_mode_.exchange(ControlMode::POSITION, std::memory_order_acq_rel);
  if (old != ControlMode::POSITION) {
    success = set_controller_mode();
  }

  mark_control_updated();
  return success;
}

// rejects speeds over the limit; sends the mode only when switching into velocity mode
bool StepperDevice::set_vel(int32_t vel_steps_s) noexcept {
  if (vel_steps_s > kMaxVelStepsPerSec || vel_steps_s < -kMaxVelStepsPerSec) {
    return false;
  }

  bool success = true;
  target_vel_.store(vel_steps_s, std::memory_order_release);

  ControlMode old = control_mode_.exchange(ControlMode::VELOCITY, std::memory_order_acq_rel);
  if (old != ControlMode::VELOCITY) {
    success = set_controller_mode();
  }

  mark_control_updated();
  return success;
}

// sends Set_Controller_Mode right away (also enables the motor)
bool StepperDevice::set_controller_mode() noexcept {
  return set_controller_mode_cmd_.send();
}

// --- RX ---
// decodes the heartbeat and the position/velocity estimates
void StepperDevice::on_rx(const CanFrame& frame) {
  switch (frame.can_simple_cmd_id()) {
    case kCmdHeartbeat:
      if (frame.dlc >= 5) {
        axis_error_.store(unpack_u32(&frame.data[0]), std::memory_order_release);
        current_axis_state_.store(frame.data[4], std::memory_order_release);
      }
      break;

    case kCmdGetEncoderEstimates:
      if (frame.dlc >= 8) {
        pos_est_.store(unpack_i32(&frame.data[0]), std::memory_order_release);
        vel_est_.store(unpack_i32(&frame.data[4]), std::memory_order_release);
      }
      break;

    default:
      break;
  }
}

// --- Watchdog ---
// on stale commands: switch to velocity 0 (skipped if the motor was never commanded)
void StepperDevice::on_soft_block() {
  if (control_mode_.load(std::memory_order_acquire) == ControlMode::NO_CONTROL) {
    return;
  }

  target_vel_.store(0, std::memory_order_release);
  ControlMode old = control_mode_.exchange(ControlMode::VELOCITY, std::memory_order_acq_rel);
  if (old != ControlMode::VELOCITY) {
    set_controller_mode();
  }
}

// --- Frame builders ---
// setpoint for the current mode; nothing before the first heartbeat and target, or on error
std::optional<CanFrame> StepperDevice::build_normal_setpoint_frame() const noexcept {


  switch (control_mode_.load(std::memory_order_acquire)) {
    case ControlMode::POSITION: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputPos, 8);
      pack_i32(&f.data[0], target_pos_.load(std::memory_order_acquire));
      return f;
    }

    case ControlMode::VELOCITY: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVel, 8);
      pack_i32(&f.data[0], target_vel_.load(std::memory_order_acquire));
      return f;
    }

    default:  // NO_CONTROL
      return std::nullopt;
  }
}

// SOFT_BLOCK: keep sending velocity 0 (only once on_soft_block switched to velocity)
std::optional<CanFrame> StepperDevice::build_safe_setpoint_frame() const noexcept {
  if (control_mode_.load(std::memory_order_acquire) != ControlMode::VELOCITY) {
    return std::nullopt;
  }

  CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVel, 8);
  pack_i32(&f.data[0], 0);
  return f;
}

// Set_Controller_Mode: mode 4 (stepper) + submode (0 = position, 1 = velocity)
std::optional<CanFrame> StepperDevice::build_controller_mode_frame() const noexcept {
  const ControlMode mode = control_mode_.load(std::memory_order_acquire);
  if (mode == ControlMode::NO_CONTROL) {
    return std::nullopt;
  }

  CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetControllerMode, 8);
  // little-endian: Control_Mode (u32) in bytes 0-3, Control_Submode (u16) in bytes 4-5
  const uint8_t bytes[8] = {0x04, 0x00, 0x00, 0x00, static_cast<uint8_t>(mode), 0x00, 0x00, 0x00};
  std::memcpy(f.data.data(), bytes, sizeof(bytes));
  return f;
}

// --- Byte helpers ---
void StepperDevice::pack_i32(uint8_t* dst, int32_t val) noexcept {
  std::memcpy(dst, &val, sizeof(int32_t));
}

uint32_t StepperDevice::unpack_u32(const uint8_t* src) noexcept {
  uint32_t val = 0;
  std::memcpy(&val, src, sizeof(uint32_t));
  return val;
}

int32_t StepperDevice::unpack_i32(const uint8_t* src) noexcept {
  int32_t val = 0;
  std::memcpy(&val, src, sizeof(int32_t));
  return val;
}