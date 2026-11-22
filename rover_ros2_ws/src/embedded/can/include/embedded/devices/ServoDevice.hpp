#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/messages/PeriodicMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class ServoDevice : public CanDeviceFactory<ServoDevice> {
public:
  // --- Setpoint (sent periodically by the TX thread) ---
  // returns false if rejected (out of range or NaN)
  bool set_pos(float pos_deg) noexcept;

protected:
  // decodes the received CAN frame (only heartbeat for the servo)
  void on_rx(const CanFrame& frame) override;

private:
  friend class CanDeviceFactory<ServoDevice>;

  // --- Construction (private: create via ServoDevice::register_device) ---
  ServoDevice(
      const std::string& name,
      uint8_t node_id,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(1750),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(50),
      std::string manager_name = "can0");

  // --- Constants ---
  // CANSimple command IDs
  static constexpr uint8_t kCmdHeartbeat = 0x001;
  static constexpr uint8_t kCmdSetInputPos = 0x00C;

  // position limits in degrees
  // TODO: ask Dhrish if the range is 0 to 180 or -90 to 90
  static constexpr float kMaxPos_ = 180.0f;
  static constexpr float kMinPos_ = 0.0f;

  // --- Frame builders (called by setpoint_periodic_) ---
  std::optional<CanFrame> build_normal_setpoint_frame() const noexcept;
  std::optional<CanFrame> build_safe_setpoint_frame() const noexcept;

  // --- Byte helpers ---
  static void pack_float(uint8_t* dst, float val) noexcept;
  static uint32_t unpack_u32(const uint8_t* src) noexcept;

  // --- Data ---
  const uint8_t node_id_;
  std::shared_ptr<PeriodicMessage> setpoint_periodic_;

  // target position (degrees)
  std::atomic<float> target_pos_{0.0f};

  // safety: nothing is sent before the first heartbeat and the first target
  std::atomic<bool> has_target_{false};
  std::atomic<bool> heartbeat_arrived_{false};

  // decoded from the heartbeat (meaningless until heartbeat_arrived_ is true)
  std::atomic<uint32_t> axis_error_{0};
  std::atomic<uint32_t> current_axis_state_{0};
};