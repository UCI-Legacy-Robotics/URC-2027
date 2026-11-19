#include "embedded/core/CanManager.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <initializer_list>

namespace {
constexpr std::size_t kInitialQueueCapacity = 256;
constexpr int kMaxWriteRetries = 4;
thread_local CanManager* current_manager_worker = nullptr;
}

std::shared_ptr<CanManager> CanManager::get_instance(const std::string& interface_name) {
  static std::mutex instance_mutex;
  static std::unordered_map<std::string, std::weak_ptr<CanManager>> instances;

  std::lock_guard<std::mutex> lock(instance_mutex);
  if (auto existing = instances[interface_name].lock()) {
    return existing;
  }
  auto created = std::shared_ptr<CanManager>(new CanManager(interface_name));
  instances[interface_name] = created;
  return created;
}

CanManager::CanManager(std::string interface_name)
    : interface_name_(std::move(interface_name)),
      rx_routes_(std::make_shared<const std::vector<RxRoute>>()) {
  std::vector<std::shared_ptr<TxMessage>> storage;
  storage.reserve(kInitialQueueCapacity);
  tx_queue_ = std::priority_queue<
      std::shared_ptr<TxMessage>,
      std::vector<std::shared_ptr<TxMessage>>,
      TxMessageComparator>(TxMessageComparator{}, std::move(storage));
}

CanManager::~CanManager() {
  stop();
}

bool CanManager::open_socket() {
  sock_fd_ = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  if (sock_fd_ < 0) {
    return false;
  }

  struct ifreq ifr {};
  std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
  if (::ioctl(sock_fd_, SIOCGIFINDEX, &ifr) < 0) {
    close_socket();
    return false;
  }

  struct sockaddr_can addr {};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    close_socket();
    return false;
  }

  return true;
}

void CanManager::close_socket() {
  if (sock_fd_ >= 0) {
    ::close(sock_fd_);
    sock_fd_ = -1;
  }
}

bool CanManager::start() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }

  const auto reap_thread = [](std::thread& thread) {
    if (!thread.joinable()) {
      return true;
    }
    if (thread.get_id() == std::this_thread::get_id()) {
      return false;
    }
    thread.join();
    return true;
  };
  if (!reap_thread(tx_thread_) || !reap_thread(rx_thread_) ||
      !reap_thread(watchdog_thread_)) {
    return false;
  }
  close_socket();

  {
    std::lock_guard<std::mutex> registry_lock(registry_mutex_);
    for (auto it = devices_.begin(); it != devices_.end();) {
      if (it->second.device.expired()) {
        it = devices_.erase(it);
      } else {
        ++it;
      }
    }
    if (devices_.empty()) {
      return false;
    }
  }

  if (!open_socket()) {
    return false;
  }

  {
    std::lock_guard<std::mutex> registry_lock(registry_mutex_);
    for (auto it = devices_.begin(); it != devices_.end();) {
      if (it->second.device.expired()) {
        it = devices_.erase(it);
      } else {
        ++it;
      }
    }
    if (devices_.empty()) {
      close_socket();
      return false;
    }
    rebuild_rx_routes_and_filters();
    running_.store(true, std::memory_order_release);
  }

  // Keep the manager alive until its worker loops have stopped.
  const auto self = shared_from_this();
  active_threads_.store(0, std::memory_order_release);
  const auto launch = [this, &self](std::thread& thread, auto loop) {
    active_threads_.fetch_add(1, std::memory_order_acq_rel);
    try {
      thread = std::thread([self, loop] {
        current_manager_worker = self.get();
        (self.get()->*loop)();
        self->thread_finished();
        current_manager_worker = nullptr;
      });
    } catch (...) {
      active_threads_.fetch_sub(1, std::memory_order_acq_rel);
      throw;
    }
  };
  try {
    launch(rx_thread_, &CanManager::rx_loop);
    launch(tx_thread_, &CanManager::tx_loop);
    launch(watchdog_thread_, &CanManager::watchdog_loop);
  } catch (...) {
    running_.store(false, std::memory_order_release);
    tx_cv_.notify_all();
    for (auto* thread : {&rx_thread_, &tx_thread_, &watchdog_thread_}) {
      if (thread->joinable()) {
        thread->join();
      }
    }
    close_socket();
    throw;
  }
  return true;
}

void CanManager::stop() {
  if (current_manager_worker == this) {
    running_.store(false, std::memory_order_release);
    tx_cv_.notify_all();
    return;
  }

  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  running_.store(false, std::memory_order_release);
  tx_cv_.notify_all();

  const auto join_or_detach = [](std::thread& thread) {
    if (!thread.joinable()) {
      return;
    }
    if (thread.get_id() == std::this_thread::get_id()) {
      thread.detach();
    } else {
      thread.join();
    }
  };
  join_or_detach(tx_thread_);
  join_or_detach(rx_thread_);
  join_or_detach(watchdog_thread_);

  {
    std::lock_guard<std::mutex> registry_lock(registry_mutex_);
    close_socket();
  }
}

void CanManager::thread_finished() {
  if (active_threads_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    std::lock_guard<std::mutex> registry_lock(registry_mutex_);
    close_socket();
  }
}

void CanManager::rebuild_rx_routes_and_filters() {
  auto next_routes = std::make_shared<std::vector<RxRoute>>();
  std::vector<struct can_filter> kernel_filters;

  next_routes->reserve(devices_.size());
  kernel_filters.reserve(devices_.size());

  for (const auto& [name, entry] : devices_) {
    const auto device = entry.device.lock();
    if (!device) {
      continue;
    }
    const CanFilter filter = device->filter();
    next_routes->push_back(RxRoute{filter, entry.device});
    struct can_filter kf {};
    kf.can_id = filter.id;
    kf.can_mask = filter.mask;
    kernel_filters.push_back(kf);
  }

  std::atomic_store(&rx_routes_, std::shared_ptr<const std::vector<RxRoute>>(next_routes));

  if (sock_fd_ >= 0) {
    if (kernel_filters.empty()) {
      ::setsockopt(sock_fd_, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);
    } else {
      ::setsockopt(
          sock_fd_,
          SOL_CAN_RAW,
          CAN_RAW_FILTER,
          kernel_filters.data(),
          static_cast<socklen_t>(kernel_filters.size() * sizeof(struct can_filter)));
    }
  }
}

bool CanManager::register_device(std::shared_ptr<CanDevice> device) {
  if (!device || device->manager_.get() != this) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto named = devices_.find(device->name());
    if (named != devices_.end() && !named->second.device.expired()) {
      return false;
    }
    if (named != devices_.end()) {
      devices_.erase(named);
    }

    for (auto it = devices_.begin(); it != devices_.end();) {
      const auto existing = it->second.device.lock();
      if (!existing) {
        it = devices_.erase(it);
        continue;
      }
      if (device->id() == existing->id() ||
          device->filter().overlaps(existing->filter())) {
        return false;
      }
      ++it;
    }

    device->registered_.store(true, std::memory_order_release);
    devices_[device->name()] = DeviceEntry{device, device.get()};
    rebuild_rx_routes_and_filters();
  }

  if (!running_.load(std::memory_order_acquire) && !start()) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = devices_.find(device->name());
    if (it != devices_.end() && it->second.device.lock() == device) {
      devices_.erase(it);
    }
    device->registered_.store(false, std::memory_order_release);
    rebuild_rx_routes_and_filters();
    return false;
  }

  // Two-phase registration: bind weak_from_this() & schedule after derived ctor finishes
  std::vector<std::shared_ptr<TxMessage>> periodic_messages;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = devices_.find(device->name());
    if (it == devices_.end() || it->second.identity != device.get() ||
        !device->registered_.load(std::memory_order_acquire)) {
      return false;
    }
    periodic_messages = device->bind_and_collect_periodic_messages();
  }
  for (auto& msg : periodic_messages) {
    schedule(std::move(msg));
  }

  return true;
}

bool CanManager::deregister_device(
    const std::string& name,
    const CanDevice* expected_device) {
  std::shared_ptr<CanDevice> removed;
  bool has_devices = false;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = devices_.find(name);
    if (it == devices_.end()) {
      return false;
    }
    if (expected_device && it->second.identity != expected_device) {
      return false;
    }
    removed = it->second.device.lock();
    if (removed) {
      removed->registered_.store(false, std::memory_order_release);
    }
    devices_.erase(it);
    rebuild_rx_routes_and_filters();

    for (const auto& [registered_name, entry] : devices_) {
      if (!entry.device.expired()) {
        has_devices = true;
        break;
      }
    }
  }

  if (removed) {
    for (auto& msg : removed->periodic_messages_) {
      if (msg) {
        msg->cancel();
      }
    }
    {
      std::lock_guard<std::mutex> lock(removed->services_mutex_);
      for (ServiceChannel* svc : removed->services_) {
        if (svc) {
          svc->cancel();
        }
      }
    }
  }

  tx_cv_.notify_one();
  if (!has_devices) {
    stop();
  }
  return true;
}

std::shared_ptr<CanDevice> CanManager::get_device(const std::string& name) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = devices_.find(name);
  return (it != devices_.end()) ? it->second.device.lock() : nullptr;
}

std::shared_ptr<CanDevice> CanManager::get_device(uint32_t id) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  for (const auto& [name, entry] : devices_) {
    if (auto device = entry.device.lock(); device && device->id() == id) {
      return device;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<CanDevice>> CanManager::get_all_devices() const {
  std::vector<std::shared_ptr<CanDevice>> out;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    out.reserve(devices_.size());
    for (const auto& [name, entry] : devices_) {
      if (auto device = entry.device.lock()) {
        out.push_back(std::move(device));
      }
    }
  }
  return out;
}

void CanManager::schedule(std::shared_ptr<TxMessage> msg) {
  if (!msg) {
    return;
  }

  msg->set_seq_id(next_seq_id_.fetch_add(1, std::memory_order_relaxed));

  bool wake_needed = false;
  {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    wake_needed = tx_queue_.empty() || (msg->next_tx() < tx_queue_.top()->next_tx());
    tx_queue_.push(std::move(msg));
  }

  if (wake_needed) {
    tx_cv_.notify_one();
  }
}

bool CanManager::write_raw_frame(const CanFrame& frame) noexcept {
  if (sock_fd_ < 0) {
    return false;
  }

  struct can_frame raw {};
  raw.can_id = frame.is_extended ? ((frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG)
                                 : (frame.id & CAN_SFF_MASK);
  raw.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
  std::memcpy(raw.data, frame.data.data(), raw.can_dlc);

  for (int attempt = 0; attempt < kMaxWriteRetries; ++attempt) {
    const ssize_t n = ::write(sock_fd_, &raw, sizeof(raw));
    if (n == static_cast<ssize_t>(sizeof(raw))) {
      return true;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  return false;
}

// -----------------------------------------------------------------------------
// Message-Agnostic Priority TX Scheduler Loop
// -----------------------------------------------------------------------------
void CanManager::tx_loop() {
  std::unique_lock<std::mutex> lock(tx_mutex_);

  while (running_.load(std::memory_order_acquire)) {
    if (tx_queue_.empty()) {
      tx_cv_.wait(lock, [this] {
        return !running_.load(std::memory_order_acquire) || !tx_queue_.empty();
      });
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto deadline = tx_queue_.top()->next_tx();
    if (deadline > now) {
      tx_cv_.wait_until(lock, deadline);
      continue;
    }

    std::shared_ptr<TxMessage> msg = tx_queue_.top();
    tx_queue_.pop();
    lock.unlock();

    TxResult outcome = msg->run(now);
    if (outcome.frame.has_value()) {
      write_raw_frame(*outcome.frame);
    }

    lock.lock();
    if (outcome.reschedule && running_.load(std::memory_order_acquire)) {
      tx_queue_.push(std::move(msg));
    }
  }
}

// -----------------------------------------------------------------------------
// Copy-On-Write Lock-Free RX Loop
// -----------------------------------------------------------------------------
void CanManager::rx_loop() {
  struct pollfd pfd {};
  pfd.fd = sock_fd_;
  pfd.events = POLLIN;

  while (running_.load(std::memory_order_acquire)) {
    const int ret = ::poll(&pfd, 1, 10);
    if (ret <= 0 || (pfd.revents & POLLIN) == 0) {
      continue;
    }

    const auto routes = std::atomic_load(&rx_routes_);

    struct can_frame raw {};
    while (::read(sock_fd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
      CanFrame frame{};
      frame.is_extended = (raw.can_id & CAN_EFF_FLAG) != 0;
      frame.id = frame.is_extended ? (raw.can_id & CAN_EFF_MASK) : (raw.can_id & CAN_SFF_MASK);
      frame.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
      std::memcpy(frame.data.data(), raw.data, frame.dlc);

      for (const auto& route : *routes) {
        if (route.filter.matches(frame)) {
          if (auto device = route.device.lock()) {
            device->handle_rx(frame);
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// 10 Hz Device Watchdog Loop
// -----------------------------------------------------------------------------
void CanManager::watchdog_loop() {
  std::vector<std::shared_ptr<CanDevice>> snapshot;
  snapshot.reserve(32);

  while (running_.load(std::memory_order_acquire)) {
    const int64_t now = CanDevice::now_ns();
    {
      snapshot.clear();
      std::lock_guard<std::mutex> lock(registry_mutex_);
      for (const auto& [name, entry] : devices_) {
        if (auto device = entry.device.lock()) {
          snapshot.push_back(std::move(device));
        }
      }
    }

    for (const auto& dev : snapshot) {
      dev->check_watchdog(now);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}