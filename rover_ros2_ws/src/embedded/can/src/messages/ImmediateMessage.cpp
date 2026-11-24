#include "embedded/messages/ImmediateMessage.hpp"
#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

#include <stdexcept>
#include <utility>

ImmediateCommand::ImmediateCommand(CanDevice& owner, FrameBuilder builder)
    : owner_(owner), builder_(std::move(builder)) {
  if (!builder_) {
    throw std::invalid_argument("Immediate command frame builder cannot be empty");
  }
}

bool ImmediateCommand::send() const {
  const auto frame = builder_();
  return frame && send(*frame);
}

bool ImmediateCommand::send(const CanFrame& frame) const {
  if (!owner_.manager_) {
    return false;
  }
  owner_.manager_->schedule(std::make_shared<ImmediateMessage>(frame));
  return true;
}

bool ImmediateCommand::send_delayed(std::chrono::microseconds delay) const {
  const auto frame = builder_();
  return frame && send_delayed(*frame, delay);
}

bool ImmediateCommand::send_delayed(
    const CanFrame& frame,
    std::chrono::microseconds delay) const {
  if (!owner_.manager_) {
    return false;
  }
  owner_.manager_->schedule(std::make_shared<ImmediateMessage>(frame, delay));
  return true;
}