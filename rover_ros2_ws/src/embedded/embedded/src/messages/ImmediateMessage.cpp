#include "embedded/messages/ImmediateMessage.hpp"
#include "embedded/core/CanDevice.hpp"

bool ImmediateCommand::send() const {
  return owner_.send_immediate(default_frame_);
}

bool ImmediateCommand::send(const CanFrame& override_frame) const {
  return owner_.send_immediate(override_frame);
}

bool ImmediateCommand::send_delayed(std::chrono::microseconds delay) const {
  return owner_.send_delayed(default_frame_, delay);
}

bool ImmediateCommand::send_delayed(
    const CanFrame& override_frame, std::chrono::microseconds delay) const {
  return owner_.send_delayed(override_frame, delay);
}