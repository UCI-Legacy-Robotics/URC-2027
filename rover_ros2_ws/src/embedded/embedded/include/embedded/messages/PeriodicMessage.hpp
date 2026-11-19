#pragma once

#include "embedded/core/TxMessage.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

class CanDevice;

class PeriodicMessage final : public TxMessage {
public:
  using FrameBuilder = std::function<std::optional<CanFrame>()>;

  PeriodicMessage(
      std::weak_ptr<CanDevice> device,
      std::chrono::microseconds period,
      FrameBuilder normal_builder,
      FrameBuilder safe_builder = nullptr,
      TimePoint start_tp = std::chrono::steady_clock::now()) noexcept;

  TxResult run(TimePoint now) override;

  // Optional per-stream pause/resume (e.g., suppressing setpoints during a mode change)
  void set_paused(bool paused) noexcept { paused_.store(paused, std::memory_order_release); }
  bool is_paused() const noexcept { return paused_.load(std::memory_order_acquire); }
  std::chrono::microseconds period() const noexcept { return period_; }

private:
  friend class CanDevice;

  void bind(std::weak_ptr<CanDevice> device, TimePoint start_tp) noexcept {
    device_ = std::move(device);
    set_next_tx(start_tp);
  }

  std::weak_ptr<CanDevice> device_;
  std::chrono::microseconds period_;
  FrameBuilder normal_builder_;
  FrameBuilder safe_builder_;
  std::atomic<bool> paused_{false};
};