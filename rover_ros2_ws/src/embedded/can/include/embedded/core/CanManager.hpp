#pragma once

#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanFrame.hpp"
#include "embedded/core/TxMessage.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class CanManager : public std::enable_shared_from_this<CanManager> {
public:
  static std::shared_ptr<CanManager> get_instance(const std::string& interface_name = "can0");

  ~CanManager();

  CanManager(const CanManager&) = delete;
  CanManager& operator=(const CanManager&) = delete;

  bool deregister_device(const std::string& name, const CanDevice* expected_device = nullptr);
  std::shared_ptr<CanDevice> get_device(const std::string& name) const;
  std::shared_ptr<CanDevice> get_device(uint32_t id) const;
  std::vector<std::shared_ptr<CanDevice>> get_all_devices() const;
  const std::string& interface_name() const noexcept { return interface_name_; }

  // Polymorphic scheduler entry point for any TxMessage subclass
  void schedule(std::shared_ptr<TxMessage> msg);

private:
  friend class CanDevice;

  explicit CanManager(std::string interface_name);

  struct RxRoute {
    CanFilter filter;
    std::weak_ptr<CanDevice> device;
  };

  struct DeviceEntry {
    std::weak_ptr<CanDevice> device;
    const CanDevice* identity;
  };

  bool open_socket();
  void close_socket();
  bool start();
  void stop();
  bool register_device(std::shared_ptr<CanDevice> device);
  void thread_finished();
  void rebuild_rx_routes_and_filters();
  bool write_raw_frame(const CanFrame& frame) noexcept;

  void tx_loop();
  void rx_loop();
  void watchdog_loop();

  const std::string interface_name_;
  int sock_fd_{-1};
  std::atomic<bool> running_{false};
  std::atomic<uint8_t> active_threads_{0};
  std::atomic<uint64_t> next_seq_id_{1};

  std::thread tx_thread_;
  std::thread rx_thread_;
  std::thread watchdog_thread_;

  std::mutex lifecycle_mutex_;
  mutable std::mutex registry_mutex_;
  std::unordered_map<std::string, DeviceEntry> devices_;

  std::shared_ptr<const std::vector<RxRoute>> rx_routes_;

  std::mutex tx_mutex_;
  std::condition_variable tx_cv_;
  std::priority_queue<
      std::shared_ptr<TxMessage>,
      std::vector<std::shared_ptr<TxMessage>>,
      TxMessageComparator>
      tx_queue_;
};