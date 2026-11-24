#include "embedded/messages/PeriodicMessage.hpp"
#include "embedded/core/CanDevice.hpp"

PeriodicMessage::PeriodicMessage(
    std::weak_ptr<CanDevice> device,
    std::chrono::microseconds period,
    FrameBuilder normal_builder,
    FrameBuilder safe_builder,
    Timestamp start_tp) noexcept
    : TxMessage(start_tp),
      device_(std::move(device)),
      period_(period),
      normal_builder_(std::move(normal_builder)),
      safe_builder_(std::move(safe_builder)) {}

TxResult PeriodicMessage::run(Timestamp now) {
  if (!is_active()) {
    return {std::nullopt, false};
  }

  // Keep CanDevice alive across lambda execution so [this] captures are always valid
  auto dev = device_.lock();
  if (!dev) {
    return {std::nullopt, false};
  }

  std::optional<CanFrame> frame = std::nullopt;

  if (!paused_.load(std::memory_order_acquire)) {
    switch (dev->state()) {
      case DeviceState::NORMAL:
        if (normal_builder_) {
          frame = normal_builder_();
        }
        break;

      case DeviceState::SOFT_BLOCK:
        if (safe_builder_) {
          frame = safe_builder_();
        }
        break;

      case DeviceState::ERROR_HARD_BLOCK:
        // Suppress periodic bus traffic while device is offline; timer keeps running
        break;
    }
  }

  Timestamp next = next_tx() + period_;
  if (next < now) {
    next = now + period_;
  }
  set_next_tx(next);

  return {frame, true};
}