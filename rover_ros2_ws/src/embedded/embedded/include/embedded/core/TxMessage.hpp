#pragma once

#include "embedded/core/CanFrame.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

struct TxResult {
  std::optional<CanFrame> frame{std::nullopt};
  bool reschedule{false};
};

class TxMessage {
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  explicit TxMessage(TimePoint initial_tx = TimePoint::min()) noexcept
      : next_tx_(initial_tx) {}

  virtual ~TxMessage() = default;

  virtual TxResult run(TimePoint now) = 0;

  void cancel() noexcept { active_.store(false, std::memory_order_release); }
  bool is_active() const noexcept { return active_.load(std::memory_order_acquire); }

  TimePoint next_tx() const noexcept { return next_tx_; }
  uint64_t seq_id() const noexcept { return seq_id_; }

protected:
  void set_next_tx(TimePoint tp) noexcept { next_tx_ = tp; }

private:
  friend class CanManager;
  void set_seq_id(uint64_t id) noexcept { seq_id_ = id; }

  TimePoint next_tx_;
  uint64_t seq_id_{0};
  std::atomic<bool> active_{true};
};

struct TxMessageComparator {
  bool operator()(
      const std::shared_ptr<TxMessage>& a,
      const std::shared_ptr<TxMessage>& b) const noexcept {
    if (a->next_tx() != b->next_tx()) {
      return a->next_tx() > b->next_tx();
    }
    return a->seq_id() > b->seq_id();
  }
};