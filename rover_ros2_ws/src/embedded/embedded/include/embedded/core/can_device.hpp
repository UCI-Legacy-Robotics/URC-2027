#pragma once

#include "embedded/control_message.hpp"
#include <linux/can.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <memory>

class CanManager;

struct PendingTransaction {
  uint32_t expected_response_id{0};
  uint8_t expected_cmd_byte{0};
  struct can_frame request_frame{};
  
  uint8_t retries_left{3};
  std::chrono::milliseconds retry_interval_ms{30};
  std::chrono::steady_clock::time_point last_sent_time;
  
  std::promise<bool> completion_promise;
};

class CanDevice {
public:
  explicit CanDevice(uint32_t rx_can_id);
  virtual ~CanDevice() = default;

  // Interface methods for derived motor hardware
  virtual void process_can_message(const struct can_frame & frame) = 0;
  virtual bool generate_periodic_frame(struct can_frame & out_frame) = 0;
  virtual std::future<bool> set_control(const ControlMessage & msg) = 0;

  // Timeout checker for async pending transactions (called by TX thread)
  virtual void check_timeouts();

  uint32_t get_rx_can_id() const { return rx_can_id_; }

  bool is_rx_healthy(uint64_t timeout_ms = 50) const {
    uint64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t last_rx = last_rx_ns_.load(std::memory_order_relaxed);
    return ((now_ns - last_rx) / 1'000'000) < timeout_ms;
  }

protected:
  void record_rx_timestamp();
  void send_immediate_event_frame(const struct can_frame & frame);

  uint32_t rx_can_id_;
  std::atomic<uint64_t> last_rx_ns_{0};
  std::atomic<uint64_t> last_tx_update_ns_{0};

  std::mutex tx_mutex_;
  std::optional<PendingTransaction> pending_tx_;

  CanManager * manager_ptr_{nullptr};
  friend class CanManager;
};