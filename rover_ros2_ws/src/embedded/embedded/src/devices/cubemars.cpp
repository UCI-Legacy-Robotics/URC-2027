// src/devices/cubemars_device.cpp
#include "embedded/devices/cubemars.hpp"
#include "embedded/devices/cubemars.hpp"
#include <iostream>

std::future<bool> CubeMarsDevice::set_control(const ControlMessage & msg) {
  // 1. Verify Message Type Safety
  if (msg.device_type != DeviceType::CUBEMARS) {
    std::promise<bool> p;
    p.set_value(false); // Wrong message type passed to this device!
    return p.get_future();
  }

  // 2. Fast zero-cost static downcast
  const auto & cm_msg = static_cast<const CubeMarsControlMessage &>(msg);

  switch (cm_msg.cmd_type) {
    case CubeMarsCommandType::MIT_MODE_SETPOINT:
    {
      // Pack full 5-parameter MIT control mode into atomic memory buffers
      target_p_des_.store(cm_msg.p_des, std::memory_order_relaxed);
      target_v_des_.store(cm_msg.v_des, std::memory_order_relaxed);
      target_kp_.store(cm_msg.kp, std::memory_order_relaxed);
      target_kd_.store(cm_msg.kd, std::memory_order_relaxed);
      target_t_ff_.store(cm_msg.t_ff, std::memory_order_relaxed);

      uint64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
      last_tx_update_ns_.store(now_ns, std::memory_order_release);

      std::promise<bool> p;
      p.set_value(true);
      return p.get_future();
    }

    case CubeMarsCommandType::ENTER_MOTOR_MODE:
    {
      struct can_frame frame{};
      frame.can_id = tx_can_id_;
      frame.can_dlc = 8;
      std::memset(frame.data, 0xFF, 7);
      frame.data[7] = 0xFC; // Enter Motor Mode Command

      send_immediate_event_frame(frame);

      std::promise<bool> p;
      p.set_value(true);
      return p.get_future();
    }
  }

  std::promise<bool> p;
  p.set_value(false);
  return p.get_future();
}