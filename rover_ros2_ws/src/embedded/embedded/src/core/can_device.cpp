#include "embedded/can_device.hpp"
#include "embedded/can_manager.hpp"

CanDevice::CanDevice(uint32_t rx_can_id) : rx_can_id_(rx_can_id) {}

void CanDevice::record_rx_timestamp() {
  uint64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
  last_rx_ns_.store(now_ns, std::memory_order_release);
}

void CanDevice::send_immediate_event_frame(const struct can_frame & frame) {
  if (manager_ptr_) {
    manager_ptr_->push_immediate_event_frame(frame);
  }
}

void CanDevice::check_timeouts() {
  std::lock_guard<std::mutex> lock(tx_mutex_);

  if (!pending_tx_.has_value()) return;

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - pending_tx_->last_sent_time);

  if (elapsed >= pending_tx_->retry_interval_ms) {
    if (pending_tx_->retries_left > 0) {
      pending_tx_->retries_left--;
      pending_tx_->last_sent_time = now;
      send_immediate_event_frame(pending_tx_->request_frame);
    } else {
      pending_tx_->completion_promise.set_value(false);
      pending_tx_.reset();
    }
  }
}