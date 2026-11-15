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
#include <type_traits>
#include <unordered_map>
#include <vector>

class CanManager : public std::enable_shared_from_this<CanManager> {
public:
  static std::shared_ptr<CanManager> get_instance(const std::string& interface_name = "can0");

  ~CanManager();

  CanManager(const CanManager&) = delete;
  CanManager& operator=(const CanManager&) = delete;

  bool start();
  void stop();

  // Idempotent lookup-or-create so ros2_control plugins & ROS components never race on startup
  template <typename T, typename... Args>
  std::shared_ptr<T> get_or_register_device(const std::string& name, Args&&... args) {
    static_assert(std::is_base_of_v<CanDevice, T>, "T must derive from CanDevice");
    if (auto existing = get_device(name)) {
      auto typed = std::dynamic_pointer_cast<T>(existing);
      if (!typed) {
        throw std::runtime_error("Device '" + name + "' already exists with a different type");
      }
      return typed;
    }

    auto dev = std::make_shared<T>(name, std::forward<Args>(args)...);
    if (!register_device(dev)) {
      if (auto concurrent = std::dynamic_pointer_cast<T>(get_device(name))) {
        return concurrent;
      }
      throw std::runtime_error("Failed to register CAN device '" + name + "'");
    }
    return dev;
  }

  bool register_device(std::shared_ptr<CanDevice> device);
  bool deregister_device(const std::string& name);
  std::shared_ptr<CanDevice> get_device(const std::string& name) const;
  std::vector<std::shared_ptr<CanDevice>> get_all_devices() const;

  // Polymorphic scheduler entry point for any TxMessage subclass
  void schedule(std::shared_ptr<TxMessage> msg);

private:
  explicit CanManager(std::string interface_name);

  struct RxRoute {
    CanFilter filter;
    std::shared_ptr<CanDevice> device;
  };

  bool open_socket();
  void close_socket();
  void rebuild_rx_routes_and_filters();
  bool write_raw_frame(const CanFrame& frame) noexcept;

  void tx_loop();
  void rx_loop();
  void watchdog_loop();

  const std::string interface_name_;
  int sock_fd_{-1};
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_seq_id_{1};

  std::thread tx_thread_;
  std::thread rx_thread_;
  std::thread watchdog_thread_;

  mutable std::mutex registry_mutex_;
  std::unordered_map<std::string, std::shared_ptr<CanDevice>> devices_;

  std::shared_ptr<const std::vector<RxRoute>> rx_routes_;

  std::mutex tx_mutex_;
  std::condition_variable tx_cv_;
  std::priority_queue<
      std::shared_ptr<TxMessage>,
      std::vector<std::shared_ptr<TxMessage>>,
      TxMessageComparator>
      tx_queue_;
};