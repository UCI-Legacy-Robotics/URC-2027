#include "embedded/devices/ODriveDevice.hpp"

#include <cstring>

ODriveDevice::ODriveDevice(
    const std::string& name,
    uint8_t node_id,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout)
    : CanDevice(name, CanFilter::can_simple(node_id), rx_timeout, control_timeout),
      node_id_(node_id),
      axis_service_(*this, [this](const CanFrame& f) {
        // ODrive acknowledges Set_Axis_State when Heartbeat byte[4] matches requested state
        return f.can_simple_cmd_id() == kCmdHeartbeat && f.dlc >= 5 &&
               f.data[4] == static_cast<uint8_t>(expected_axis_state_.load(std::memory_order_acquire));
      }) {
  // Stream active control setpoint at 200 Hz (every 5000 us)
  add_periodic(0, std::chrono::microseconds(5000));
}

void ODriveDevice::set_pos(float pos_rev) noexcept {
  target_pos_.store(pos_rev, std::memory_order_release);
  control_mode_.store(ControlMode::POSITION, std::memory_order_release);
  mark_control_updated();
}

void ODriveDevice::set_vel(float vel_rev_s) noexcept {
  target_vel_.store(vel_rev_s, std::memory_order_release);
  control_mode_.store(ControlMode::VELOCITY, std::memory_order_release);
  mark_control_updated();
}

void ODriveDevice::set_torque(float torque_nm) noexcept {
  target_torque_.store(torque_nm, std::memory_order_release);
  control_mode_.store(ControlMode::TORQUE, std::memory_order_release);
  mark_control_updated();
}

bool ODriveDevice::set_axis(uint32_t requested_state) {
  expected_axis_state_.store(requested_state, std::memory_order_release);

  CanFrame req = CanFrame::make_can_simple(node_id_, kCmdSetAxisState, 4);
  pack_u32(req.data.data(), requested_state);

  // Send immediately and retry every 50 ms up to 5 times until Heartbeat confirms state
  return axis_service_.send(req, std::chrono::milliseconds(50), 5);
}

bool ODriveDevice::set_origin(float origin_pos_rev) {
  CanFrame frame = CanFrame::make_can_simple(node_id_, kCmdSetAbsolutePos, 4);
  pack_float(frame.data.data(), origin_pos_rev);
  return send_immediate(frame);
}

void ODriveDevice::on_rx(const CanFrame& frame) {
  switch (frame.can_simple_cmd_id()) {
    case kCmdHeartbeat:
      if (frame.dlc >= 5) {
        axis_error_.store(unpack_u32(&frame.data[0]), std::memory_order_release);
        current_axis_state_.store(frame.data[4], std::memory_order_release);
      }
      break;

    case kCmdGetEncoderEstimates:
      if (frame.dlc >= 8) {
        pos_est_.store(unpack_float(&frame.data[0]), std::memory_order_release);
        vel_est_.store(unpack_float(&frame.data[4]), std::memory_order_release);
      }
      break;

    case kCmdGetTorques:
      if (frame.dlc >= 8) {
        // Bytes [0..3] = Torque Target, Bytes [4..7] = Torque Estimate
        torque_est_.store(unpack_float(&frame.data[4]), std::memory_order_release);
      }
      break;

    default:
      break;
  }
}

std::optional<CanFrame> ODriveDevice::on_periodic_tx(uint8_t) {
  switch (control_mode_.load(std::memory_order_acquire)) {
    case ControlMode::POSITION: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputPos, 8);
      pack_float(&f.data[0], target_pos_.load(std::memory_order_acquire));
      // Bytes [4..5] Vel_FF = 0, Bytes [6..7] Torque_FF = 0
      return f;
    }

    case ControlMode::VELOCITY: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVel, 8);
      pack_float(&f.data[0], target_vel_.load(std::memory_order_acquire));
      pack_float(&f.data[4], 0.0f);  // Torque feedforward
      return f;
    }

    case ControlMode::TORQUE: {
      CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputTorque, 4);
      pack_float(&f.data[0], target_torque_.load(std::memory_order_acquire));
      return f;
    }
  }
  return std::nullopt;
}

std::optional<CanFrame> ODriveDevice::on_safe_tx(uint8_t) {
  // Automatically transmitted when control_timeout expires (SOFT_BLOCK)
  CanFrame f = CanFrame::make_can_simple(node_id_, kCmdSetInputVel, 8);
  pack_float(&f.data[0], 0.0f);
  pack_float(&f.data[4], 0.0f);
  return f;
}

void ODriveDevice::pack_float(uint8_t* dst, float val) noexcept {
  std::memcpy(dst, &val, sizeof(float));
}

void ODriveDevice::pack_u32(uint8_t* dst, uint32_t val) noexcept {
  std::memcpy(dst, &val, sizeof(uint32_t));
}

float ODriveDevice::unpack_float(const uint8_t* src) noexcept {
  float val = 0.0f;
  std::memcpy(&val, src, sizeof(float));
  return val;
}

uint32_t ODriveDevice::unpack_u32(const uint8_t* src) noexcept {
  uint32_t val = 0;
  std::memcpy(&val, src, sizeof(uint32_t));
  return val;
}