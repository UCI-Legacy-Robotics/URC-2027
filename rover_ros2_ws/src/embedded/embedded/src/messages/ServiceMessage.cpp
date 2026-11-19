#include "embedded/messages/ServiceMessage.hpp"
#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

#include <algorithm>

ServiceMessage::ServiceMessage(
    std::weak_ptr<ServiceChannel> channel,
    uint32_t generation,
    const CanFrame& request,
    std::chrono::milliseconds retry_interval,
    uint8_t max_retries) noexcept
    : TxMessage(Timestamp::min()),
      channel_(std::move(channel)),
      generation_(generation),
      request_(request),
      retry_interval_(retry_interval),
      max_attempts_(static_cast<uint8_t>(1 + max_retries)) {}

TxResult ServiceMessage::run(Timestamp now) {
  if (!is_active()) {
    return {std::nullopt, false};
  }

  auto ch = channel_.lock();
  if (!ch || !ch->is_generation_waiting(generation_)) {
    return {std::nullopt, false};
  }

  if (attempts_made_ < max_attempts_) {
    ++attempts_made_;
    set_next_tx(now + retry_interval_);
    return {request_, true};
  }

  ch->mark_timed_out(generation_);
  return {std::nullopt, false};
}

ServiceChannel::ServiceChannel(CanDevice& owner, AckMatcher matcher)
    : owner_(owner), matcher_(std::move(matcher)) {
  std::lock_guard<std::mutex> lock(owner_.services_mutex_);
  owner_.services_.push_back(this);
}

ServiceChannel::~ServiceChannel() {
  cancel();
  std::lock_guard<std::mutex> lock(owner_.services_mutex_);
  auto& vec = owner_.services_;
  vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
}

bool ServiceChannel::send(
    const CanFrame& req,
    std::chrono::milliseconds retry_interval,
    uint8_t max_retries) {
  auto mgr = owner_.manager_.lock();
  if (!mgr) {
    return false;
  }

  const uint32_t gen = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  auto msg = std::make_shared<ServiceMessage>(
      weak_from_this(), gen, req, retry_interval, max_retries);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_msg_) {
      active_msg_->cancel();
    }
    active_msg_ = msg;
    response_.reset();
    state_.store(ServiceState::WAITING, std::memory_order_release);
  }

  mgr->schedule(std::move(msg));
  return true;
}

bool ServiceChannel::try_match_ack(const CanFrame& frame) {
  if (state_.load(std::memory_order_acquire) != ServiceState::WAITING) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load(std::memory_order_relaxed) != ServiceState::WAITING) {
    return false;
  }
  if (!matcher_ || !matcher_(frame)) {
    return false;
  }

  if (active_msg_) {
    active_msg_->cancel();
    active_msg_.reset();
  }
  response_ = frame;
  state_.store(ServiceState::RECEIVED, std::memory_order_release);
  return true;
}

bool ServiceChannel::is_generation_waiting(uint32_t gen) const noexcept {
  return state_.load(std::memory_order_acquire) == ServiceState::WAITING &&
         generation_.load(std::memory_order_acquire) == gen;
}

void ServiceChannel::mark_timed_out(uint32_t gen) noexcept {
  if (generation_.load(std::memory_order_acquire) != gen) {
    return;
  }
  ServiceState expected = ServiceState::WAITING;
  state_.compare_exchange_strong(expected, ServiceState::TIMEOUT, std::memory_order_acq_rel);
}

std::optional<CanFrame> ServiceChannel::take_response() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto out = std::move(response_);
  response_.reset();
  if (state_.load(std::memory_order_relaxed) == ServiceState::RECEIVED) {
    state_.store(ServiceState::IDLE, std::memory_order_release);
  }
  return out;
}

void ServiceChannel::cancel() {
  generation_.fetch_add(1, std::memory_order_acq_rel);
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_msg_) {
    active_msg_->cancel();
    active_msg_.reset();
  }
  state_.store(ServiceState::IDLE, std::memory_order_release);
}