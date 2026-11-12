#pragma once
#include "embedded/core/control_message.hpp"
#include <cstdint>

enum class ServoCommandType : uint8_t {
  NONE,             // must stay first for now, set_control rejects
  SET_INPUT_POS
};

struct ServoControlMessage : public ControlMessage {
    // ODRIVE is pending for now, need to clarify with Dhrish 
    ServoControlMessage() : ControlMessage(DeviceType::ODRIVE) {}

    ServoCommandType cmd_type{ServoCommandType::NONE};
    
    // positions parameters
    float p_des{0.0f};  // Desired position (rev)
    // velocity and torque are left out, and always set to 0
};