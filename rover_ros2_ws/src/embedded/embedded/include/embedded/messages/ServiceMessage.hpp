#pragma once

#include "embedded/core/CanFrame.hpp"
#include "embedded/core/TxMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class CanDevice;
class ServiceChannel;

enum class ServiceState : uint8_t {
  IDLE,
  WAITING,
  RECEIVED,
  TIMEOUT
};

class ServiceMessage final : public TxMessage {
public:
  ServiceMessage(
      std::weak_ptr<ServiceChannel> channel,
      uint32_t generation,
      const CanFrame& request,
      std::chrono::milliseconds retry_interval,
      uint8_t max_retries) noexcept;

  TxResult run(Timestamp now) override;

private:
  std::weak_ptr<ServiceChannel> channel_;
  uint32_t generation_;
  CanFrame request_;
  std::chrono::milliseconds retry_interval_;
  uint8_t attempts_made_{0};
  uint8_t max_attempts_;
};

class ServiceChannel : public std::enable_shared_from_this<ServiceChannel> {
public:
  using AckMatcher = std::function<bool(const CanFrame&)>;

  ServiceChannel(CanDevice& owner, AckMatcher matcher);
  ~ServiceChannel();

  bool send(const CanFrame& req, std::chrono::milliseconds retry_interval, uint8_t max_retries);
  std::optional<CanFrame> take_response();
  ServiceState state() const noexcept { return state_.load(std::memory_order_acquire); }
  void cancel();

private:
  friend class CanDevice;
  friend class ServiceMessage;

  bool try_match_ack(const CanFrame& frame);
  bool is_generation_waiting(uint32_t gen) const noexcept;
  void mark_timed_out(uint32_t gen) noexcept;

  CanDevice& owner_;
  AckMatcher matcher_;
  std::atomic<ServiceState> state_{ServiceState::IDLE};
  std::atomic<uint32_t> generation_{0};

  std::mutex mutex_;
  std::shared_ptr<ServiceMessage> active_msg_;
  std::optional<CanFrame> response_;
};