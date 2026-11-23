#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/messages/ImmediateMessage.hpp"
#include "embedded/messages/PeriodicMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class StepperDevice : public CanDeviceFactory<StepperDevice> {
public:
  // chage if the master can sheet changes the encoding
  enum class ControlMode : uint8_t {
    POSITION = 0,
    VELOCITY = 1,
    NO_CONTROL = 0xFF
  };

  // --- Continuous Setpoints (Sampled at 200 Hz by setpoint_periodic_) ---
  bool set_pos(int32_t pos_steps) noexcept;
  bool set_vel(int32_t vel_steps_s) noexcept;

  // --- Lock-Free Telemetry Getters (Updated by rx_loop) ---
  int32_t get_pos() const noexcept { return pos_est_.load(std::memory_order_acquire); }
  int32_t get_vel() const noexcept { return vel_est_.load(std::memory_order_acquire); }
  uint32_t get_axis_state() const noexcept {
    return current_axis_state_.load(std::memory_order_acquire);
  }
  uint32_t get_axis_error() const noexcept {
    return axis_error_.load(std::memory_order_acquire);
  }

protected:
  void on_rx(const CanFrame& frame) override;

private:
  friend class CanDeviceFactory<StepperDevice>;

  StepperDevice(
      const std::string& name,
      uint8_t node_id,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(100),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(50),
      std::string manager_name = "can0");

  // Stepper CANSimple Command IDs
  static constexpr uint8_t kCmdHeartbeat = 0x001;
  static constexpr uint8_t kCmdGetEncoderEstimates = 0x009;
  static constexpr uint8_t kCmdSetControllerMode = 0x00B;
  static constexpr uint8_t kCmdSetInputPos = 0x00C;
  static constexpr uint8_t kCmdSetInputVel = 0x00D;
  // velocity limit
  static constexpr uint8_t kMaxVelStepsPerSec = 180;

  std::optional<CanFrame> build_normal_setpoint_frame() const noexcept;
  std::optional<CanFrame> build_safe_setpoint_frame() const noexcept;
  std::optional<CanFrame> build_controller_mode_frame() const noexcept;
  bool set_controller_mode() noexcept;

  static void pack_u32(uint8_t* dst, uint32_t val) noexcept;
  static void pack_u16(uint8_t* dst, uint16_t val) noexcept;
  static void pack_i32(uint8_t* dst, int32_t val) noexcept;
  static uint32_t unpack_u32(const uint8_t* src) noexcept;
  static int32_t unpack_i32(const uint8_t* src) noexcept;

  const uint8_t node_id_;

  std::atomic<ControlMode> control_mode_{ControlMode::NO_CONTROL};
  std::atomic<int32_t> target_pos_{0};
  std::atomic<int32_t> target_vel_{0};

  std::atomic<int32_t> pos_est_{0};
  std::atomic<int32_t> vel_est_{0};
  std::atomic<uint32_t> current_axis_state_{0};
  std::atomic<uint32_t> axis_error_{0};

  // safety: nothing is sent before the first heartbeat and the first target
  std::atomic<bool> has_target_{false};
  std::atomic<bool> heartbeat_arrived_{false};


  // Explicit TX Message Members
  std::shared_ptr<PeriodicMessage> setpoint_periodic_;
  ImmediateCommand set_controller_mode_cmd_;
};