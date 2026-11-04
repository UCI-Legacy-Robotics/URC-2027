#pragma once
#include <cstdint>

enum class DeviceType : uint8_t {
  GENERIC,
  ODRIVE,
  CUBEMARS
};

// Base class for all control messages
struct ControlMessage {
  explicit ControlMessage(DeviceType type) : device_type(type) {}
  virtual ~ControlMessage() = default;

  const DeviceType device_type;
};