#pragma once

#include "embedded/core/CanFrame.hpp"
#include "embedded/core/ServiceTx.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class CanManager;
class CanDevice;

enum class DeviceState : uint8_t {
  NORMAL,
  SOFT_BLOCK,
  ERROR_HARD_BLOCK
};

struct TxTask {
  enum class Type : uint8_t {
    PERIODIC,
    SERVICE_RETRY
  };

  Type type{Type::PERIODIC};
  uint8_t task_id{0};
  std::chrono::microseconds period{0};
  std::chrono::steady_clock::time_point deadline{};
  std::weak_ptr<CanDevice> device{};
  std::atomic<bool> active{true};

  // Used only when type == SERVICE_RETRY
  CanFrame frame{};
  uint8_t retries_left{0};
  std::function<void()> on_retry_tick{};
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

protected:
  // --- Teammate Overrides ---
  virtual void on_rx(const CanFrame& frame) = 0;
  virtual std::optional<CanFrame> on_periodic_tx(uint8_t task_id) { (void)task_id; return std::nullopt; }
  virtual std::optional<CanFrame> on_safe_tx(uint8_t task_id) { (void)task_id; return std::nullopt; }
  virtual void on_soft_block() {}
  virtual void on_hard_block() {}

  // --- Teammate Helpers ---
  void add_periodic(uint8_t task_id, std::chrono::microseconds period);
  bool send_immediate(const CanFrame& frame);
  void mark_control_updated() noexcept;

private:
  friend class CanManager;
  friend class ServiceTx;

  struct PeriodicSpec {
    uint8_t task_id;
    std::chrono::microseconds period;
  };

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
  std::vector<PeriodicSpec> periodic_specs_;
  std::vector<std::shared_ptr<TxTask>> owned_tasks_;

  std::mutex services_mutex_;
  std::vector<ServiceTx*> services_;
};