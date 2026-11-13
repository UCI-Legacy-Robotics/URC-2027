#pragma once

#include "embedded/core/CanFrame.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class CanDevice;
struct TxTask;

enum class ServiceState : uint8_t {
  IDLE,
  WAITING,
  RECEIVED,
  TIMEOUT
};

class ServiceTx {
public:
  using AckMatcher = std::function<bool(const CanFrame&)>;

  ServiceTx(CanDevice& owner, AckMatcher matcher);
  ~ServiceTx();

  bool send(const CanFrame& req, std::chrono::milliseconds retry_interval, uint8_t max_retries);
  std::optional<CanFrame> take_response();
  ServiceState state() const noexcept { return state_.load(std::memory_order_acquire); }
  void cancel();

private:
  friend class CanDevice;
  bool try_match_ack(const CanFrame& frame);

  CanDevice& owner_;
  AckMatcher matcher_;
  std::atomic<ServiceState> state_{ServiceState::IDLE};

  std::mutex mutex_;
  std::shared_ptr<TxTask> active_task_;
  std::optional<CanFrame> response_;
};