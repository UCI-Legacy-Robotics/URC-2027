#include "embedded/devices/ServoDevice.hpp"

#include <cmath>
#include <cstring>
#include <utility>

// --- Construction ---
ServoDevice::ServoDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    std::string manager_name)
    : CanDeviceFactory<ServoDevice>(
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
          [this]() { return build_safe_setpoint_frame(); })) {}

// --- Setpoint ---
// stores the target if it's in range and not NaN; returns false if rejected
bool ServoDevice::set_pos(float pos_deg) noexcept {
  if (pos_deg < kMinPos_ || pos_deg > kMaxPos_ || std::isnan(pos_deg)) {
    return false;
  }
  target_pos_.store(pos_deg, std::memory_order_release);
  has_target_.store(true, std::memory_order_release);

  // records a fresh command, so the watchdog stays out of SOFT_BLOCK
  mark_control_updated();
  return true;
}

// --- RX ---
// decodes the heartbeat (the only message the servo sends)
void ServoDevice::on_rx(const CanFrame& frame) {
  if (frame.can_simple_cmd_id() == kCmdHeartbeat && frame.dlc >= 5) {
    axis_error_.store(unpack_u32(&frame.data[0]), std::memory_order_release);
    current_axis_state_.store(frame.data[4], std::memory_order_release);
    heartbeat_arrived_.store(true, std::memory_order_release);
  }
}

// --- Frame builders ---
// sends the target only after the first heartbeat and the first target, and no error
std::optional<CanFrame> ServoDevice::build_normal_setpoint_frame() const noexcept {
  if (heartbeat_arrived_.load(std::memory_order_acquire) &&
      has_target_.load(std::memory_order_acquire) &&
      axis_error_.load(std::memory_order_acquire) == 0) {
    CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputPos, 8);
    // target position in bytes 0-3 (Set_Input_Pos layout)
    pack_float(&f.data[0], target_pos_.load(std::memory_order_acquire));
    return f;
  }
  return std::nullopt;
}

// SOFT_BLOCK: send nothing, stop the servo
std::optional<CanFrame> ServoDevice::build_safe_setpoint_frame() const noexcept {
  return std::nullopt;
}

// --- Byte helpers ---
void ServoDevice::pack_float(uint8_t* dst, float val) noexcept {
  std::memcpy(dst, &val, sizeof(float));
}

uint32_t ServoDevice::unpack_u32(const uint8_t* src) noexcept {
  uint32_t val = 0;
  std::memcpy(&val, src, sizeof(uint32_t));
  return val;
}