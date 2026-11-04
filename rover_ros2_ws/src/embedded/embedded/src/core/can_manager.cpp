#include "embedded/can_manager.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

std::shared_ptr<CanManager> CanManager::get_instance(const std::string & interface_name) {
  static std::mutex static_mutex;
  std::lock_guard<std::mutex> lock(static_mutex);
  static std::unordered_map<std::string, std::shared_ptr<CanManager>> instances;

  if (instances.find(interface_name) == instances.end()) {
    instances[interface_name] = std::shared_ptr<CanManager>(new CanManager(interface_name));
  }
  return instances[interface_name];
}

CanManager::CanManager(std::string interface_name)
: interface_name_(std::move(interface_name)) {
  event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

CanManager::~CanManager() {
  stop();
  if (event_fd_ >= 0) ::close(event_fd_);
}

bool CanManager::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return true; // Already running
  }

  sock_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock_ < 0) {
    running_ = false;
    return false;
  }

  struct ifreq ifr;
  std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
  if (::ioctl(sock_, SIOCGIFINDEX, &ifr) < 0) {
    ::close(sock_);
    sock_ = -1;
    running_ = false;
    return false;
  }

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(sock_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(sock_);
    sock_ = -1;
    running_ = false;
    return false;
  }

  int flags = ::fcntl(sock_, F_GETFL, 0);
  ::fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

  rx_thread_ = std::thread(&CanManager::rx_loop, this);
  tx_thread_ = std::thread(&CanManager::tx_loop, this);
  return true;
}

void CanManager::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  notify_worker_event();

  if (rx_thread_.joinable()) rx_thread_.join();
  if (tx_thread_.joinable()) tx_thread_.join();

  if (sock_ >= 0) {
    ::close(sock_);
    sock_ = -1;
  }
}

void CanManager::register_device(const std::string & name, 
                                 std::shared_ptr<CanDevice> device, 
                                 double update_hz) 
{
  std::lock_guard<std::mutex> lock(registry_mutex_);
  named_registry_[name] = device;
  id_registry_[device->get_rx_can_id()] = device;

  NamedRegistrationRequest req{};
  std::strncpy(req.name, name.c_str(), sizeof(req.name) - 1);
  req.device = device;
  registration_queue_.push(req);

  PeriodicEntry entry;
  entry.device = device;
  entry.interval = std::chrono::microseconds(static_cast<int64_t>(1'000'000.0 / update_hz));
  entry.next_tx = std::chrono::steady_clock::now();
  periodic_entries_.push_back(entry);

  start();
  notify_worker_event();
  notify_tx_worker();
}

std::shared_ptr<CanDevice> CanManager::get_device(const std::string & name) {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = named_registry_.find(name);
  return (it != named_registry_.end()) ? it->second : nullptr;
}

std::shared_ptr<CanDevice> CanManager::get_device(uint32_t rx_can_id) {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = id_registry_.find(rx_can_id);
  return (it != id_registry_.end()) ? it->second : nullptr;
}

void CanManager::push_immediate_event_frame(const struct can_frame & frame) {
  event_tx_queue_.push(frame);
  notify_tx_worker();
}

std::future<bool> CanManager::send_and_receive(const struct can_frame & frame,
                                                uint32_t expected_rx_id,
                                                uint8_t expected_ack_byte,
                                                uint8_t max_retries,
                                                std::chrono::milliseconds timeout) 
{
  auto req = new SendAndReceiveRequest();
  req->frame = frame;
  req->expected_rx_id = expected_rx_id;
  req->expected_ack_byte = expected_ack_byte;
  req->max_retries = max_retries;
  req->timeout = timeout;
  req->promise = std::make_shared<std::promise<bool>>();

  auto future = req->promise->get_future();
  ack_queue_.push(req);
  notify_tx_worker();

  return future;
}

void CanManager::notify_worker_event() {
  uint64_t sig = 1;
  ::write(event_fd_, &sig, sizeof(sig));
}

void CanManager:notify_tx_worker() {
  uint64_t sig = 1;
  ::write(tx_event_fd_, &sig, sizeof(sig));
}

void CanManager::rx_loop() {
  std::unordered_map<uint32_t, std::shared_ptr<CanDevice>> local_device_map;
  NamedRegistrationRequest req;

  struct pollfd fds[2];
  fds[0].fd = sock_;      fds[0].events = POLLIN | POLLERR | POLLHUP;
  fds[1].fd = event_fd_;  fds[1].events = POLLIN;

  struct can_frame frame;

  while (running_.load(std::memory_order_relaxed)) {
    // Blocks indefinitely (-1 timeout). Thread sleeps with 0% CPU until woken by kernel.
    int ret = ::poll(fds, 2, -1);

    if (ret < 0) {
      if (errno == EINTR) continue; // Interrupted by signal, retry safely
      break; // Real socket error, break to shutdown thread cleanly
    }

    // -------------------------------------------------------------------------
    // HARDWARE ERROR / TEARDOWN PATH (Prevents 100% CPU spinning on errors)
    // -------------------------------------------------------------------------
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      // Interface went down or socket was closed externally
      break; 
    }

    // -------------------------------------------------------------------------
    // HOT PATH: Incoming CAN Telemetry Frames
    // -------------------------------------------------------------------------
    if (fds[0].revents & POLLIN) {
      while (::read(sock_, &frame, sizeof(struct can_frame)) > 0) {
        uint32_t id = frame.can_id & CAN_SFF_MASK;

        // 1. Check if frame matches an active Send & Receive transaction
        {
          std::lock_guard<std::mutex> lock(tx_map_mutex_);
          auto it = pending_transactions_.find(rx_id);
          if (it != pending_transactions_.end()) {
            if (frame.data[0] == it->second.req.expected_ack_byte) {
              it->second.req.promise->set_value(true);
              pending_transactions_.erase(it);
              continue; // ACK consumed
            }
          }
        }

        // 2. Dispatch to target device for periodic telemetry processing
        std::shared_ptr<CanDevice> dev = nullptr;
        {
          std::lock_guard<std::mutex> lock(registry_mutex_);
          auto it = id_registry_.find(rx_id);
          if (it != id_registry_.end()) dev = it->second;
        }

        if (dev) {
          dev->process_can_message(frame);
        }
      }
    }

    // -------------------------------------------------------------------------
    // COLD PATH: Inter-Thread Device Registration Wakeup
    // -------------------------------------------------------------------------
    if (fds[1].revents & POLLIN) {
      uint64_t dummy;
      // Drain eventfd buffer
      ::read(event_fd_, &dummy, sizeof(dummy));

      // Pop newly registered devices into the local thread map
      while (registration_queue_.pop(req)) {
        local_device_map[req.device->get_rx_can_id()] = req.device;
      }
    }
  }
}

void CanManager::tx_loop() {
  struct pollfd pfd;
  pfd.fd = tx_event_fd_; // Woken up immediately when send_event() or registration occurs
  pfd.events = POLLIN;

  std::vector<PeriodicEntry> local_periodic_entries;
  TxRegistrationRequest tx_req;
  struct can_frame frame;
  SendAndReceiveRequest * ack_req = nullptr;

  while (running_.load(std::memory_order_relaxed)) {
    auto now = std::chrono::steady_clock::now();

    // -------------------------------------------------------------------------
    // 1. COLD PATH: Drain New Registrations
    // -------------------------------------------------------------------------
    while (tx_registration_queue_.pop(tx_req)) {
      PeriodicEntry entry;
      entry.device = tx_req.device;
      entry.interval = tx_req.interval;
      entry.next_tx = now;
      local_periodic_entries.push_back(entry);
    }

    // -------------------------------------------------------------------------
    // 2. HOT PATH: Flush High-Priority Events & Service Messages (INSTANT SDOs)
    // -------------------------------------------------------------------------
    while (event_queue_.pop(frame)) {
      ::send(sock_, &frame, sizeof(struct can_frame), 0);
    }

    while (ack_queue_.pop(ack_req)) {
      ::send(sock_, &ack_req->frame, sizeof(struct can_frame), 0);
      
      PendingTransaction pending;
      pending.req = *ack_req;
      pending.retries_left = ack_req->max_retries;
      pending.last_sent = now;

      {
        std::lock_guard<std::mutex> lock(tx_map_mutex_);
        pending_transactions_[ack_req->expected_rx_id] = pending;
      }
      delete ack_req;
    }

    // Process SDO retries/timeouts...
    process_pending_transaction_retries(now);

    // -------------------------------------------------------------------------
    // 3. HOT PATH: Sample Due Periodic Frames & Find Next Wakeup Target
    // -------------------------------------------------------------------------
    auto next_wake_target = now + std::chrono::hours(1); // Default far-future target

    for (auto & entry : local_periodic_entries) {
      if (now >= entry.next_tx) {
        if (entry.device->generate_periodic_frame(frame)) {
          ::send(sock_, &frame, sizeof(struct can_frame), 0);
        }
        // Advance schedule precisely to avoid drift
        entry.next_tx += entry.interval; 
      }

      if (entry.next_tx < next_wake_target) {
        next_wake_target = entry.next_tx;
      }
    }

    // -------------------------------------------------------------------------
    // 4. DYNAMIC SLEEP: Calculate exact time until next periodic frame is due
    // -------------------------------------------------------------------------
    now = std::chrono::steady_clock::now();
    int timeout_ms = 10; // Default fallback sleep

    if (!local_periodic_entries.empty() && next_wake_target > now) {
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(next_wake_target - now);
      timeout_ms = std::max(0, static_cast<int>(dur.count()));
    }

    // Sleep until EITHER the next periodic frame is due OR tx_event_fd_ wakes us up instantly!
    int ret = ::poll(&pfd, 1, timeout_ms);

    if (ret > 0 && (pfd.revents & POLLIN)) {
      uint64_t dummy;
      ::read(tx_event_fd_, &dummy, sizeof(dummy)); // Drain wakeup notification
    }
  }
}