#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/messages/ImmediateMessage.hpp"
#include "embedded/messages/PeriodicMessage.hpp"
#include "embedded/messages/ServiceMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class YellowJacketDevice : public CanDevice {
public:
  enum class ControlMode : uint8_t {
    POSITION = 4,
    VELOCITY = 3,
    DUTY_CYCLE = 1
  };

  YellowJacketDevice(
      const std::string& name,
      uint8_t node_id,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(200),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(100));

  // --- Continuous Setpoints (Sampled at 200 Hz by setpoint_periodic_) ---
  void set_pos(float pos_rev) noexcept;
  void set_vel(float vel_rev_s) noexcept;
  void set_duty(int32_t duty_pwm) noexcept;

  // --- Lock-Free Telemetry Getters (Updated by rx_loop) ---
  float get_pos() const noexcept { return pos_est_.load(std::memory_order_acquire); }
  float get_vel() const noexcept { return vel_est_.load(std::memory_order_acquire); }

protected:
  void on_rx(const CanFrame& frame) override;

private:
  // YellowJacket CANSimple Command IDs
  static constexpr uint8_t kCmdHeartbeat = 0x001;
  static constexpr uint8_t kCmdGetEncoderEstimates = 0x009;
  static constexpr uint8_t kCmdSetControllerMode = 0x00B;
  static constexpr uint8_t kCmdSetInputPos = 0x00C;
  static constexpr uint8_t kCmdSetInputVelandDuty = 0x00D;

  std::optional<CanFrame> build_normal_setpoint_frame() const noexcept;
  std::optional<CanFrame> build_safe_setpoint_frame() const noexcept;
  bool set_controller_mode() noexcept;

  const uint8_t node_id_;

  std::atomic<ControlMode> control_mode_{ControlMode::VELOCITY};
  std::atomic<float> target_pos_{0.0f};
  std::atomic<float> target_vel_{0.0f};
  std::atomic<int32_t> target_duty_{0.0f};

  std::atomic<float> pos_est_{0.0f};
  std::atomic<float> vel_est_{0.0f};

  // Explicit TX Message Members
  std::shared_ptr<PeriodicMessage> setpoint_periodic_;
  ImmediateCommand set_controller_mode_cmd_;
};