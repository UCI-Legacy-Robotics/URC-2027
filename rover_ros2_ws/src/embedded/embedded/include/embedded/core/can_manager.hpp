#pragma once

#include "embedded/can_device.hpp"
#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

struct NamedRegistrationRequest {
  char name[32];
  std::shared_ptr<CanDevice> device;
};

class CanManager {
public:
  static std::shared_ptr<CanManager> get_instance(const std::string & interface_name = "can0");
  ~CanManager();

  bool start();
  void stop();

  void register_device(const std::string & name, std::shared_ptr<CanDevice> device);
  std::shared_ptr<CanDevice> get_device(const std::string & name);
  std::shared_ptr<CanDevice> get_device(uint32_t rx_can_id);

  void push_immediate_event_frame(const struct can_frame & frame);

private:
  explicit CanManager(std::string interface_name);

  void rx_loop();
  void tx_loop();
  void notify_worker_event();

  std::string interface_name_;
  int sock_{-1};
  int event_fd_{-1};
  std::atomic<bool> running_{false};

  std::thread rx_thread_;
  std::thread tx_thread_;

  // Fast read-only mutex cache for shared memory lookup across threads
  std::mutex registry_mutex_;
  std::unordered_map<std::string, std::shared_ptr<CanDevice>> named_registry_;
  std::unordered_map<uint32_t, std::shared_ptr<CanDevice>> id_registry_;

  // Lock-Free Queues for high-speed hot-path processing
  boost::lockfree::queue<struct can_frame> event_tx_queue_{128};
  boost::lockfree::queue<NamedRegistrationRequest> registration_queue_{64};
};