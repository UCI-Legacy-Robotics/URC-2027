#pragma once

#include "embedded/core/CanFrame.hpp"
#include "embedded/core/TxMessage.hpp"
#include "embedded/messages/ImmediateMessage.hpp"
#include "embedded/messages/PeriodicMessage.hpp"
#include "embedded/messages/ServiceMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CanManager;

enum class DeviceState : uint8_t {
  NORMAL,
  SOFT_BLOCK,
  ERROR_HARD_BLOCK
};

class CanDevice : public std::enable_shared_from_this<CanDevice> {
public:
  CanDevice(
      std::string name,
      CanFilter filter,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(100),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(0));

  virtual ~CanDevice();

  const std::string& name() const noexcept { return name_; }
  CanFilter filter() const noexcept { return filter_; }
  DeviceState state() const noexcept { return state_.load(std::memory_order_acquire); }
  bool is_healthy() const noexcept { return state() != DeviceState::ERROR_HARD_BLOCK; }

  bool send_immediate(const CanFrame& frame);
  bool send_delayed(const CanFrame& frame, std::chrono::microseconds delay);
  bool schedule_message(std::shared_ptr<TxMessage> msg);

protected:
  // --- Device Subclass Overrides ---
  virtual void on_rx(const CanFrame& frame) = 0;
  virtual void on_soft_block() {}
  virtual void on_hard_block() {}

  // --- Device Subclass Factories for Member Initialization ---
  // Creates a two-lambda PeriodicMessage (normal + soft-block degraded) queued upon registration
  std::shared_ptr<PeriodicMessage> create_periodic(
      std::chrono::microseconds period,
      PeriodicMessage::FrameBuilder normal_builder,
      PeriodicMessage::FrameBuilder safe_builder = nullptr);

  std::shared_ptr<ServiceChannel> create_service(ServiceChannel::AckMatcher matcher);

  void mark_control_updated() noexcept;

private:
  friend class CanManager;
  friend class ServiceChannel;

  struct PendingPeriodicSpec {
    std::shared_ptr<PeriodicMessage> msg;
    std::chrono::microseconds period;
  };

  std::vector<std::shared_ptr<TxMessage>> bind_and_collect_periodic_messages();
  void handle_rx(const CanFrame& frame);
  void check_watchdog(int64_t now_ns);
  static int64_t now_ns() noexcept;

  const std::string name_;
  const CanFilter filter_;
  const int64_t rx_timeout_ns_;
  const int64_t control_timeout_ns_;

  std::atomic<DeviceState> state_{DeviceState::NORMAL};
  std::atomic<int64_t> last_rx_ns_{0};
  std::atomic<int64_t> last_control_ns_{0};
  std::atomic<uint8_t> recovery_count_{0};

  std::weak_ptr<CanManager> manager_;
  std::vector<PendingPeriodicSpec> periodic_specs_;
  std::vector<std::shared_ptr<PeriodicMessage>> periodic_messages_;

  std::mutex services_mutex_;
  std::vector<ServiceChannel*> services_;
};