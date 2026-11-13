#include "embedded/core/ServiceTx.hpp"
#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

#include <algorithm>

ServiceTx::ServiceTx(CanDevice& owner, AckMatcher matcher)
    : owner_(owner), matcher_(std::move(matcher)) {
  std::lock_guard<std::mutex> lock(owner_.services_mutex_);
  owner_.services_.push_back(this);
}

ServiceTx::~ServiceTx() {
  cancel();
  std::lock_guard<std::mutex> lock(owner_.services_mutex_);
  auto& s = owner_.services_;
  s.erase(std::remove(s.begin(), s.end(), this), s.end());
}

bool ServiceTx::send(
    const CanFrame& req,
    std::chrono::milliseconds retry_interval,
    uint8_t max_retries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load(std::memory_order_acquire) == ServiceState::WAITING) {
    return false;
  }

  if (active_task_) {
    active_task_->active.store(false, std::memory_order_release);
  }

  response_.reset();
  state_.store(ServiceState::WAITING, std::memory_order_release);

  if (max_retries > 0) {
    if (auto mgr = owner_.manager_.lock()) {
      auto task = std::make_shared<TxTask>();
      task->type = TxTask::Type::SERVICE_RETRY;
      task->period = std::chrono::duration_cast<std::chrono::microseconds>(retry_interval);
      task->deadline = std::chrono::steady_clock::now() + task->period;
      task->device = owner_.weak_from_this();
      task->frame = req;
      task->retries_left = max_retries;

      task->on_retry_tick = [this, raw_task = task.get()]() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (active_task_.get() == raw_task && state_.load() == ServiceState::WAITING) {
          state_.store(ServiceState::TIMEOUT, std::memory_order_release);
        }
      };

      active_task_ = task;
      mgr->schedule_task(std::move(task));
    }
  }

  if (!owner_.send_immediate(req)) {
    if (active_task_) {
      active_task_->active.store(false, std::memory_order_release);
    }
    state_.store(ServiceState::IDLE, std::memory_order_release);
    return false;
  }
  return true;
}

std::optional<CanFrame> ServiceTx::take_response() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load(std::memory_order_acquire) != ServiceState::RECEIVED) {
    return std::nullopt;
  }
  auto out = std::move(response_);
  response_.reset();
  state_.store(ServiceState::IDLE, std::memory_order_release);
  return out;
}

void ServiceTx::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_task_) {
    active_task_->active.store(false, std::memory_order_release);
    active_task_.reset();
  }
  state_.store(ServiceState::IDLE, std::memory_order_release);
}

bool ServiceTx::try_match_ack(const CanFrame& frame) {
  if (state_.load(std::memory_order_acquire) != ServiceState::WAITING) {
    return false;
  }
  if (!matcher_ || !matcher_(frame)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load(std::memory_order_acquire) != ServiceState::WAITING) {
    return false;
  }
  if (active_task_) {
    active_task_->active.store(false, std::memory_order_release);
  }
  response_ = frame;
  state_.store(ServiceState::RECEIVED, std::memory_order_release);
  return true;
}