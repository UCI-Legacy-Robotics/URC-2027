#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

namespace {
constexpr uint8_t kRecoveryFramesRequired = 3;
}

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
  for (auto& msg : periodic_messages_) {
    if (msg) {
      msg->cancel();
    }
  }
  std::lock_guard<std::mutex> lock(services_mutex_);
  for (ServiceChannel* svc : services_) {
    if (svc) {
      svc->cancel();
    }
  }
}

int64_t CanDevice::now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::shared_ptr<PeriodicMessage> CanDevice::create_periodic(
    std::chrono::microseconds period,
    PeriodicMessage::FrameBuilder normal_builder,
    PeriodicMessage::FrameBuilder safe_builder) {
  // weak_from_this() is empty during base/derived constructors; bound in CanManager::register_device()
  auto msg = std::make_shared<PeriodicMessage>(
      std::weak_ptr<CanDevice>{},
      period,
      std::move(normal_builder),
      std::move(safe_builder),
      std::chrono::steady_clock::now());

  periodic_specs_.push_back(PendingPeriodicSpec{msg, period});
  periodic_messages_.push_back(msg);
  return msg;
}

std::vector<std::shared_ptr<TxMessage>> CanDevice::bind_and_collect_periodic_messages() {
  const auto start_tp = std::chrono::steady_clock::now();
  const auto self_weak = weak_from_this();

  std::vector<std::shared_ptr<TxMessage>> out;
  out.reserve(periodic_specs_.size());

  for (auto& spec : periodic_specs_) {
    spec.msg->bind(self_weak, start_tp);
    out.push_back(spec.msg);
  }
  periodic_specs_.clear();
  return out;
}

std::shared_ptr<ServiceChannel> CanDevice::create_service(ServiceChannel::AckMatcher matcher) {
  return std::make_shared<ServiceChannel>(*this, std::move(matcher));
}

bool CanDevice::send_immediate(const CanFrame& frame) {
  return schedule_message(std::make_shared<ImmediateMessage>(frame));
}

bool CanDevice::send_delayed(const CanFrame& frame, std::chrono::microseconds delay) {
  return schedule_message(std::make_shared<ImmediateMessage>(frame, delay));
}

bool CanDevice::schedule_message(std::shared_ptr<TxMessage> msg) {
  auto mgr = manager_.lock();
  if (!mgr || !msg) {
    return false;
  }
  mgr->schedule(std::move(msg));
  return true;
}

void CanDevice::mark_control_updated() noexcept {
  last_control_ns_.store(now_ns(), std::memory_order_release);

  DeviceState expected = DeviceState::SOFT_BLOCK;
  state_.compare_exchange_strong(expected, DeviceState::NORMAL, std::memory_order_acq_rel);
}

void CanDevice::handle_rx(const CanFrame& frame) {
  const int64_t t = now_ns();
  last_rx_ns_.store(t, std::memory_order_release);

  if (state_.load(std::memory_order_acquire) == DeviceState::ERROR_HARD_BLOCK) {
    const uint8_t count = recovery_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (count >= kRecoveryFramesRequired) {
      recovery_count_.store(0, std::memory_order_release);
      const bool ctrl_stale =
          (control_timeout_ns_ > 0) &&
          ((t - last_control_ns_.load(std::memory_order_acquire)) > control_timeout_ns_);
      state_.store(
          ctrl_stale ? DeviceState::SOFT_BLOCK : DeviceState::NORMAL,
          std::memory_order_release);
    }
  }

  {
    std::lock_guard<std::mutex> lock(services_mutex_);
    for (ServiceChannel* svc : services_) {
      if (svc && svc->try_match_ack(frame)) {
        break;
      }
    }
  }

  on_rx(frame);
}

void CanDevice::check_watchdog(int64_t now) {
  if (rx_timeout_ns_ > 0 &&
      (now - last_rx_ns_.load(std::memory_order_acquire)) > rx_timeout_ns_) {
    recovery_count_.store(0, std::memory_order_release);
    const DeviceState prev =
        state_.exchange(DeviceState::ERROR_HARD_BLOCK, std::memory_order_acq_rel);
    if (prev != DeviceState::ERROR_HARD_BLOCK) {
      on_hard_block();
    }
    return;
  }

  if (control_timeout_ns_ > 0 &&
      state_.load(std::memory_order_acquire) == DeviceState::NORMAL &&
      (now - last_control_ns_.load(std::memory_order_acquire)) > control_timeout_ns_) {
    DeviceState expected = DeviceState::NORMAL;
    if (state_.compare_exchange_strong(
            expected, DeviceState::SOFT_BLOCK, std::memory_order_acq_rel)) {
      on_soft_block();
    }
  }
}