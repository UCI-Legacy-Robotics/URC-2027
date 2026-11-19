#pragma once

#include "embedded/core/CanFrame.hpp"
#include "embedded/core/TxMessage.hpp"

#include <chrono>
#include <functional>
#include <optional>

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

// Builder-backed device command that snapshots a fresh immutable frame on each send.
class ImmediateCommand {
public:
  using FrameBuilder = std::function<std::optional<CanFrame>()>;

  ImmediateCommand(CanDevice& owner, FrameBuilder builder);

  // Builder-backed commands snapshot their frame on the calling thread.
  bool send() const;
  bool send(const CanFrame& frame) const;
  bool send_delayed(std::chrono::microseconds delay) const;
  bool send_delayed(const CanFrame& frame, std::chrono::microseconds delay) const;

private:
  CanDevice& owner_;
  FrameBuilder builder_;
};