#include "embedded/core/CanManager.hpp"

#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

std::shared_ptr<CanManager> CanManager::get_instance(const std::string& interface_name) {
  static std::mutex m;
  static std::unordered_map<std::string, std::shared_ptr<CanManager>> instances;

  std::lock_guard<std::mutex> lock(m);
  auto& inst = instances[interface_name];
  if (!inst) {
    inst = std::shared_ptr<CanManager>(new CanManager(interface_name));
  }
  return inst;
}

CanManager::CanManager(std::string interface_name)
    : interface_name_(std::move(interface_name)),
      rx_routes_(std::make_shared<std::vector<RxRoute>>()) {}

CanManager::~CanManager() {
  stop();
}

bool CanManager::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }

  const int fd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
  if (fd < 0) {
    return false;
  }

  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
  if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    ::close(fd);
    return false;
  }

  const int recv_own = 0;
  ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own));

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return false;
  }

  sock_fd_ = fd;
  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    sync_filters_locked();
  }

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

  if (watchdog_thread_.joinable()) watchdog_thread_.join();
  if (tx_thread_.joinable()) tx_thread_.join();
  if (rx_thread_.joinable()) rx_thread_.join();

  if (sock_fd_ >= 0) {
    ::close(sock_fd_);
    sock_fd_ = -1;
  }
}

bool CanManager::register_device(const std::shared_ptr<CanDevice>& device) {
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
      (void)existing_name;
      if (device->filter().overlaps(existing_dev->filter())) {
        return false;
      }
    }

    device->manager_ = weak_from_this();
    devices_.emplace(device->name(), device);
    sync_filters_locked();
  }

  // Schedule periodic tasks with a slight stagger so initial frames don't burst together
  const auto stagger = std::chrono::microseconds(250 * (stagger_idx_.fetch_add(1) % 32));
  const auto start_time = std::chrono::steady_clock::now() + stagger;

  for (const auto& spec : device->periodic_specs_) {
    auto task = std::make_shared<TxTask>();
    task->type = TxTask::Type::PERIODIC;
    task->task_id = spec.task_id;
    task->period = spec.period;
    task->deadline = start_time + spec.period;
    task->device = device;
    device->owned_tasks_.push_back(task);
    schedule_task(std::move(task));
  }

  return true;
}

bool CanManager::unregister_device(const std::string& name) {
  std::shared_ptr<CanDevice> removed;
  bool empty = false;

  {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = devices_.find(name);
    if (it == devices_.end()) {
      return false;
    }
    removed = std::move(it->second);
    devices_.erase(it);
    sync_filters_locked();
    empty = devices_.empty();
  }

  if (removed) {
    for (auto& t : removed->owned_tasks_) {
      if (t) t->active.store(false, std::memory_order_release);
    }
    removed->owned_tasks_.clear();
  }

  if (empty) {
    stop();
  }
  return true;
}

std::shared_ptr<CanDevice> CanManager::get_device(const std::string& name) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  auto it = devices_.find(name);
  return it != devices_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<CanDevice>> CanManager::get_all_devices() const {
  std::lock_guard<std::mutex> lock(registry_mutex_);
  std::vector<std::shared_ptr<CanDevice>> out;
  out.reserve(devices_.size());
  for (const auto& [name, dev] : devices_) {
    (void)name;
    out.push_back(dev);
  }
  return out;
}

bool CanManager::send_immediate(const CanFrame& frame) {
  if (!running_.load(std::memory_order_acquire)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    immediate_queue_.push_back(frame);
  }
  tx_cv_.notify_one();
  return true;
}

void CanManager::schedule_task(std::shared_ptr<TxTask> task) {
  {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    periodic_queue_.push(std::move(task));
  }
  tx_cv_.notify_one();
}

void CanManager::sync_filters_locked() {
  auto new_routes = std::make_shared<std::vector<RxRoute>>();
  std::vector<struct can_filter> kernel_filters;
  new_routes->reserve(devices_.size());
  kernel_filters.reserve(devices_.size());

  for (const auto& [name, dev] : devices_) {
    (void)name;
    new_routes->push_back({dev->filter(), dev});
    kernel_filters.push_back({dev->filter().id, dev->filter().mask});
  }

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

  std::atomic_store(&rx_routes_, std::shared_ptr<const std::vector<RxRoute>>(std::move(new_routes)));
}

bool CanManager::write_socket_frame(const CanFrame& frame) noexcept {
  struct can_frame raw{};
  raw.can_id = frame.is_extended ? ((frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG)
                                 : (frame.id & CAN_SFF_MASK);
  raw.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
  std::memcpy(raw.data, frame.data.data(), raw.can_dlc);

  // Retry briefly if MCP2515 SPI TX buffers are temporarily full (ENOBUFS / EAGAIN)
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (::write(sock_fd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
      return true;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(150));
  }
  return false;
}

void CanManager::rx_loop() {
  struct pollfd pfd{sock_fd_, POLLIN, 0};

  while (running_.load(std::memory_order_acquire)) {
    if (::poll(&pfd, 1, 50) <= 0 || (pfd.revents & POLLIN) == 0) {
      continue;
    }

    auto routes = std::atomic_load(&rx_routes_);
    struct can_frame raw{};

    while (::read(sock_fd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
      if ((raw.can_id & CAN_ERR_FLAG) != 0U) {
        continue;
      }

      CanFrame frame{};
      frame.is_extended = (raw.can_id & CAN_EFF_FLAG) != 0U;
      frame.id = frame.is_extended ? (raw.can_id & CAN_EFF_MASK) : (raw.can_id & CAN_SFF_MASK);
      frame.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
      std::memcpy(frame.data.data(), raw.data, frame.dlc);

      for (const auto& route : *routes) {
        if (route.filter.matches(frame)) {
          if (auto dev = route.device.lock()) {
            dev->handle_rx(frame);
          }
          break;
        }
      }
    }
  }
}

void CanManager::tx_loop() {
  std::unique_lock<std::mutex> lock(tx_mutex_);

  while (running_.load(std::memory_order_acquire)) {
    // 1. Send all immediate frames first
    while (!immediate_queue_.empty()) {
      CanFrame frame = immediate_queue_.front();
      immediate_queue_.pop_front();
      lock.unlock();
      write_socket_frame(frame);
      lock.lock();
    }

    // 2. Process all due periodic and service-retry tasks
    const auto now = std::chrono::steady_clock::now();
    while (!periodic_queue_.empty()) {
      auto top = periodic_queue_.top();
      if (!top || !top->active.load(std::memory_order_acquire)) {
        periodic_queue_.pop();
        continue;
      }
      if (top->deadline > now) {
        break;
      }

      periodic_queue_.pop();
      auto dev = top->device.lock();
      if (!dev) {
        continue;
      }

      lock.unlock();

      if (top->type == TxTask::Type::PERIODIC) {
        std::optional<CanFrame> out;
        if (dev->state() == DeviceState::NORMAL) {
          out = dev->on_periodic_tx(top->task_id);
        } else if (dev->state() == DeviceState::SOFT_BLOCK) {
          out = dev->on_safe_tx(top->task_id);
        }
        if (out) {
          write_socket_frame(*out);
        }

        top->deadline += top->period;
        const auto current = std::chrono::steady_clock::now();
        if (top->deadline < current) {
          top->deadline = current + top->period;
        }

        lock.lock();
        if (top->active.load(std::memory_order_acquire)) {
          periodic_queue_.push(std::move(top));
        }
      } else {
        // SERVICE_RETRY
        if (top->retries_left > 0) {
          write_socket_frame(top->frame);
          --top->retries_left;
        }
        if (top->retries_left == 0) {
          top->active.store(false, std::memory_order_release);
          if (top->on_retry_tick) top->on_retry_tick();
          lock.lock();
        } else {
          top->deadline = std::chrono::steady_clock::now() + top->period;
          lock.lock();
          if (top->active.load(std::memory_order_acquire)) {
            periodic_queue_.push(std::move(top));
          }
        }
      }
    }

    // 3. Sleep until either a new immediate frame arrives or the next task deadline hits
    if (!immediate_queue_.empty()) {
      continue;
    }
    if (periodic_queue_.empty()) {
      tx_cv_.wait(lock);
    } else {
      tx_cv_.wait_until(lock, periodic_queue_.top()->deadline);
    }
  }
}

void CanManager::watchdog_loop() {
  while (running_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 10 Hz
    if (!running_.load(std::memory_order_acquire)) {
      break;
    }
    const int64_t t = CanDevice::now_ns();
    for (const auto& dev : get_all_devices()) {
      if (dev) {
        dev->check_watchdog(t);
      }
    }
  }
}