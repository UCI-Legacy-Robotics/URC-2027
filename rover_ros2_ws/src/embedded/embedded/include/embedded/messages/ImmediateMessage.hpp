#pragma once

#include "embedded/core/CanFrame.hpp"
#include "embedded/core/TxMessage.hpp"

#include <chrono>

class CanDevice;

class ImmediateMessage final : public TxMessage {
public:
  explicit ImmediateMessage(const CanFrame& frame) noexcept
      : TxMessage(Timestamp::min()), frame_(frame) {}

  ImmediateMessage(const CanFrame& frame, std::chrono::microseconds delay) noexcept
      : TxMessage(std::chrono::steady_clock::now() + delay), frame_(frame) {}

  TxResult run(Timestamp /*now*/) override {
    if (!is_active()) {
      return {std::nullopt, false};
    }
    return {frame_, false};
  }

private:
  CanFrame frame_;
};

// Member descriptor for derived devices: outlines fixed one-shot commands in the class header
// while spawning a fresh immutable ImmediateMessage per send() so back-to-back calls never race.
class ImmediateCommand {
public:
  ImmediateCommand(CanDevice& owner, const CanFrame& default_frame) noexcept
      : owner_(owner), default_frame_(default_frame) {}

  bool send() const;
  bool send(const CanFrame& override_frame) const;
  bool send_delayed(std::chrono::microseconds delay) const;
  bool send_delayed(const CanFrame& override_frame, std::chrono::microseconds delay) const;

private:
  CanDevice& owner_;
  CanFrame default_frame_;
};