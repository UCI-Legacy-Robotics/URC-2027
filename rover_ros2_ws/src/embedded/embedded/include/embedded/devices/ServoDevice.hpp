#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/core/ServiceTx.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

class ServoDevice : public CanDevice {
public:
  ServoDevice(
      const std::string& name,
      uint8_t node_id,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(1750),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(50));

  // --- Continuous Setpoints (Sent Periodically by tx_loop) ---
  // noexcept: doesn't throw exceptions
  // returns false if rejected (out of range or NaN)
  bool set_pos(float pos_deg) noexcept;

protected:
  // on_rx decodes the received CAN Frame
  void on_rx(const CanFrame& frame) override;
  std::optional<CanFrame> on_periodic_tx(uint8_t task_id) override;
  std::optional<CanFrame> on_safe_tx(uint8_t task_id) override;

private:
  // CANSimple Command IDs
  static constexpr uint8_t kCmdHeartbeat = 0x001;
  static constexpr uint8_t kCmdSetInputPos = 0x00C;

  // helper fns
  static void pack_float(uint8_t* dst, float val) noexcept;
  static uint32_t unpack_u32(const uint8_t* src) noexcept;

  const uint8_t node_id_;

  // used only for the first cmd. Makes sure no cmd sent before the first heartbeat & target arrived
  std::atomic<bool> has_target_{false};
  std::atomic<bool> heartbeat_arrived_{false};

  // position vars
  static constexpr float kMaxPos_ = 180.0f; // ask dhrish is is -90 to 90 or 0 to 180
  static constexpr float kMinPos_ = 0.0f;
  std::atomic<float> target_pos_{0.0f};

  std::atomic<uint32_t> current_axis_state_{0};
  std::atomic<uint32_t> axis_error_{0};
};