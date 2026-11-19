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

namespace {
constexpr std::size_t kInitialQueueCapacity = 256;
constexpr int kMaxWriteRetries = 4;
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
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }
  if (!open_socket()) {
    return false;
  }

  rebuild_rx_routes_and_filters();

  running_.store(true, std::memory_order_release);
  rx_thread_ = std::thread(&CanManager::rx_loop, this);
  tx_thread_ = std::thread(&CanManager::tx_loop, this);
  watchdog_thread_ = std::thread(&CanManager::watchdog_loop, this);
  return true;
}

void CanManager::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  tx_cv_.notify_all();

  if (tx_thread_.joinable()) {
    tx_thread_.join();
  }
  if (rx_thread_.joinable()) {
    rx_thread_.join();
  }
  if (watchdog_thread_.joinable()) {
    watchdog_thread_.join();
  }

  close_socket();
}

void CanManager::rebuild_rx_routes_and_filters() {
  auto next_routes = std::make_shared<std::vector<RxRoute>>();
  std::vector<struct can_filter> kernel_filters;

  next_routes->reserve(devices_.size());
  kernel_filters.reserve(devices_.size());

  for (const auto& [name, dev] : devices_) {
    next_routes->push_back(RxRoute{dev->filter(), dev});
    struct can_filter kf {};
    kf.can_id = dev->filter().id;
    kf.can_mask = dev->filter().mask;
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
  if (!device) {
    return false;
  }

  if (!running_.load(std::memory_order_acquire) && !start()) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    if (devices_.count(device->name()) > 0) {
      return false;
    }
    for (const auto& [existing_name, existing_dev] : devices_) {
      if (device->filter().overlaps(existing_dev->filter())) {
        return false;
      }
    }
    device->manager_ = weak_from_this();
    devices_[device->name()] = device;
    rebuild_rx_routes_and_filters();
  }

  // Two-phase registration: bind weak_from_this() & schedule after derived ctor finishes
  for (auto& msg : device->bind_and_collect_periodic_messages()) {
    schedule(std::move(msg));
  }

  return true;
}

bool CanManager::deregister_device(const std::string& name) {
  std::shared_ptr<CanDevice> removed;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = devices_.find(name);
    if (it == devices_.end()) {
      return false;
    }
    removed = std::move(it->second);
    devices_.erase(it);
    rebuild_rx_routes_and_filters();
  }

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

  tx_cv_.notify_one();
  return true;
}

std::shared_ptr<CanDevice> CanManager::get_device(const std::string& name) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = devices_.find(name);
  return (it != devices_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<CanDevice>> CanManager::get_all_devices() const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  std::vector<std::shared_ptr<CanDevice>> out;
  out.reserve(devices_.size());
  for (const auto& [name, dev] : devices_) {
    out.push_back(dev);
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
          route.device->handle_rx(frame);
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
      std::lock_guard<std::mutex> lock(registry_mutex_);
      snapshot.clear();
      for (const auto& [name, dev] : devices_) {
        snapshot.push_back(dev);
      }
    }

    for (const auto& dev : snapshot) {
      dev->check_watchdog(now);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}