#pragma once
#include "embedded/core/control_message.hpp"

enum class CubeMarsCommandType : uint8_t {
  MIT_MODE_SETPOINT,
  ENTER_MOTOR_MODE,
  EXIT_MOTOR_MODE,
  SET_ZERO_POSITION
};

struct CubeMarsControlMessage : public ControlMessage {
  CubeMarsControlMessage() : ControlMessage(DeviceType::CUBEMARS) {}

  CubeMarsCommandType cmd_type{CubeMarsCommandType::MIT_MODE_SETPOINT};
  
  // Custom MIT Mode parameters for impedance/force control
  float p_des{0.0f};  // Desired position (rad)
  float v_des{0.0f};  // Desired velocity (rad/s)
  float kp{0.0f};     // Position gain (N·m/rad)
  float kd{0.0f};     // Velocity gain (N·m·s/rad)
  float t_ff{0.0f};   // Feedforward torque (N·m)
};