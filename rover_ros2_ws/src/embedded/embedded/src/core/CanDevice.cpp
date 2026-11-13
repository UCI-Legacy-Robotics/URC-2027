#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

CanDevice::CanDevice(
    std::string name,
    CanFilter filter,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout)
    : name_(std::move(name)),
      filter_(filter),
      rx_timeout_ns_(std::chrono::duration_cast<std::chrono::nanoseconds>(rx_timeout).count()),
      control_timeout_ns_(
          std::chrono::duration_cast<std::chrono::nanoseconds>(control_timeout).count()) {
  const int64_t t = now_ns();
  last_rx_ns_.store(t, std::memory_order_relaxed);
  last_control_ns_.store(t, std::memory_order_relaxed);
}

CanDevice::~CanDevice() {
  for (auto& t : owned_tasks_) {
    if (t) {
      t->active.store(false, std::memory_order_release);
    }
  }
}

int64_t CanDevice::now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void CanDevice::add_periodic(uint8_t task_id, std::chrono::microseconds period) {
  periodic_specs_.push_back({task_id, period});
}

bool CanDevice::send_immediate(const CanFrame& frame) {
  if (auto mgr = manager_.lock()) {
    return mgr->send_immediate(frame);
  }
  return false;
}

void CanDevice::mark_control_updated() noexcept {
  last_control_ns_.store(now_ns(), std::memory_order_release);
  DeviceState expected = DeviceState::SOFT_BLOCK;
  state_.compare_exchange_strong(expected, DeviceState::NORMAL, std::memory_order_acq_rel);
}

void CanDevice::handle_rx(const CanFrame& frame) {
  const int64_t t = now_ns();
  last_rx_ns_.store(t, std::memory_order_release);

  // Debounce recovery from ERROR_HARD_BLOCK (requires 3 consecutive frames)
  if (state_.load(std::memory_order_acquire) == DeviceState::ERROR_HARD_BLOCK) {
    if (recovery_count_.fetch_add(1, std::memory_order_acq_rel) + 1 >= 3) {
      const bool stale_ctrl =
          (control_timeout_ns_ > 0) && ((t - last_control_ns_.load()) > control_timeout_ns_);
      DeviceState expected = DeviceState::ERROR_HARD_BLOCK;
      if (state_.compare_exchange_strong(
              expected, stale_ctrl ? DeviceState::SOFT_BLOCK : DeviceState::NORMAL)) {
        recovery_count_.store(0, std::memory_order_release);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(services_mutex_);
    for (auto* svc : services_) {
      if (svc && svc->try_match_ack(frame)) {
        break;
      }
    }
  }

  on_rx(frame);
}

void CanDevice::check_watchdog(int64_t current_ns) {
  const DeviceState cur = state_.load(std::memory_order_acquire);

  // 1. Hardware RX timeout -> ERROR_HARD_BLOCK
  if (rx_timeout_ns_ > 0 && (current_ns - last_rx_ns_.load(std::memory_order_acquire)) > rx_timeout_ns_) {
    if (cur != DeviceState::ERROR_HARD_BLOCK) {
      DeviceState expected = cur;
      if (state_.compare_exchange_strong(expected, DeviceState::ERROR_HARD_BLOCK)) {
        recovery_count_.store(0, std::memory_order_release);
        on_hard_block();
      }
    }
    return;
  }

  // 2. Higher-level control setpoint timeout -> SOFT_BLOCK
  if (control_timeout_ns_ > 0 && cur == DeviceState::NORMAL) {
    if ((current_ns - last_control_ns_.load(std::memory_order_acquire)) > control_timeout_ns_) {
      DeviceState expected = DeviceState::NORMAL;
      if (state_.compare_exchange_strong(expected, DeviceState::SOFT_BLOCK)) {
        on_soft_block();
      }
    }
  }
}