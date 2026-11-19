
#include "embedded/devices/ServoDevice.hpp"
#include <cstring>
#include <cmath>
#include <optional>

// constructor for ServoDevice 
ServoDevice::ServoDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout)
    : CanDevice(name, CanFilter::can_simple(node_id), rx_timeout, control_timeout),
      node_id_(node_id) {
  // Stream active control setpoint at 50Hz -> 20ms period
  add_periodic(0, std::chrono::milliseconds(20));
}

// update the target pos, if it is in range, and is not NaN. Returns true if success. 
bool ServoDevice::set_pos(float pos_deg) noexcept {
  if (pos_deg < kMinPos_ || pos_deg > kMaxPos_ || std::isnan(pos_deg)) {
    return false;
  }
  target_pos_.store(pos_deg, std::memory_order_release);
  has_target_.store(true, std::memory_order_release);

  mark_control_updated();
  return true;
}

// decodes the received CAN Frame (only heartbeat in servo)
void ServoDevice::on_rx(const CanFrame& frame) {
  // check if the arrived frame is the heartbeat, and if at least 5 bytes
  if (frame.can_simple_cmd_id() == kCmdHeartbeat && frame.dlc >= 5) {
    axis_error_.store(unpack_u32(&frame.data[0]), std::memory_order_release);
    current_axis_state_.store(frame.data[4], std::memory_order_release);
    heartbeat_arrived_.store(true, std::memory_order_release);
  }
}

std::optional<CanFrame> ServoDevice::on_periodic_tx(uint8_t) {
  // makes sure no postion is sent before the target is set, and the heatbeat arrived
  // heartbeat_arrived_ makes sure that we are reading real axis_error
  if (heartbeat_arrived_.load(std::memory_order_acquire) == true && 
      has_target_.load(std::memory_order_acquire) == true && 
      axis_error_.load(std::memory_order_acquire) == 0) {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputPos, 8);
      // start at byte 0 according to the CAN Table
      pack_float(&f.data[0], target_pos_.load(std::memory_order_acquire));
      return f;
    }
  return std::nullopt;
}

// sends nothing if in safe block
std::optional<CanFrame> ServoDevice::on_safe_tx(uint8_t) {
  return std::nullopt;
}

void ServoDevice::pack_float(uint8_t* dst, float val) noexcept {
  std::memcpy(dst, &val, sizeof(float));
}

uint32_t ServoDevice::unpack_u32(const uint8_t* src) noexcept {
  uint32_t val = 0;
  std::memcpy(&val, src, sizeof(uint32_t));
  return val;
}