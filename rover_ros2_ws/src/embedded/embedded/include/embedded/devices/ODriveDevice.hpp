#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/core/ServiceTx.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

class ODriveDevice : public CanDevice {
public:
  enum class ControlMode : uint8_t {
    POSITION,
    VELOCITY,
    TORQUE
  };

  // ODrive Axis States (CANSimple CMD 0x007)
  static constexpr uint32_t kAxisStateIdle = 1;
  static constexpr uint32_t kAxisStateClosedLoop = 8;

  ODriveDevice(
      const std::string& name,
      uint8_t node_id,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(100),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(50));

  // --- Continuous Setpoints (Sent Periodically by tx_loop) ---
  void set_pos(float pos_rev) noexcept;
  void set_vel(float vel_rev_s) noexcept;
  void set_torque(float torque_nm) noexcept;

  // --- Telemetry Getters (Updated Lock-Free by rx_loop) ---
  float get_pos() const noexcept { return pos_est_.load(std::memory_order_acquire); }
  float get_vel() const noexcept { return vel_est_.load(std::memory_order_acquire); }
  float get_torque() const noexcept { return torque_est_.load(std::memory_order_acquire); }
  uint32_t get_axis_state() const noexcept { return current_axis_state_.load(std::memory_order_acquire); }
  uint32_t get_axis_error() const noexcept { return axis_error_.load(std::memory_order_acquire); }

  // --- Service Message with ACK (Retries until Heartbeat confirms target state) ---
  bool set_axis(uint32_t requested_state);
  ServiceState axis_service_state() const noexcept { return axis_service_.state(); }

  // --- Immediate Message (Dispatched immediately, no retries) ---
  bool set_origin(float origin_pos_rev = 0.0f);

protected:
  void on_rx(const CanFrame& frame) override;
  std::optional<CanFrame> on_periodic_tx(uint8_t task_id) override;
  std::optional<CanFrame> on_safe_tx(uint8_t task_id) override;

private:
  // ODrive CANSimple Command IDs
  static constexpr uint8_t kCmdHeartbeat = 0x001;
  static constexpr uint8_t kCmdSetAxisState = 0x007;
  static constexpr uint8_t kCmdGetEncoderEstimates = 0x009;
  static constexpr uint8_t kCmdSetInputPos = 0x00C;
  static constexpr uint8_t kCmdSetInputVel = 0x00D;
  static constexpr uint8_t kCmdSetInputTorque = 0x00E;
  static constexpr uint8_t kCmdSetAbsolutePos = 0x019;
  static constexpr uint8_t kCmdGetTorques = 0x01C;

  static void pack_float(uint8_t* dst, float val) noexcept;
  static void pack_u32(uint8_t* dst, uint32_t val) noexcept;
  static float unpack_float(const uint8_t* src) noexcept;
  static uint32_t unpack_u32(const uint8_t* src) noexcept;

  const uint8_t node_id_;

  std::atomic<ControlMode> control_mode_{ControlMode::VELOCITY};
  std::atomic<float> target_pos_{0.0f};
  std::atomic<float> target_vel_{0.0f};
  std::atomic<float> target_torque_{0.0f};

  std::atomic<float> pos_est_{0.0f};
  std::atomic<float> vel_est_{0.0f};
  std::atomic<float> torque_est_{0.0f};
  std::atomic<uint32_t> current_axis_state_{0};
  std::atomic<uint32_t> axis_error_{0};

  std::atomic<uint32_t> expected_axis_state_{0};
  ServiceTx axis_service_;
};