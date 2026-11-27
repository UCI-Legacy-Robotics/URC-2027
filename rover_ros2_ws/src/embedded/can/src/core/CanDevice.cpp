#include "embedded/core/CanDevice.hpp"
#include "embedded/core/CanManager.hpp"

#include <stdexcept>

namespace {
constexpr uint8_t kRecoveryFramesRequired = 3;
}

CanDevice::CanDevice(
    std::string name,
    CanFilter filter,
    std::chrono::milliseconds rx_timeout,
    std::chrono::milliseconds control_timeout,
    std::string manager_name,
    std::optional<uint32_t> id)
    : name_(std::move(name)),
      manager_name_(std::move(manager_name)),
      filter_(filter),
      id_(id.value_or(filter.id)),
      rx_timeout_ns_(std::chrono::duration_cast<std::chrono::nanoseconds>(rx_timeout).count()),
      control_timeout_ns_(
          std::chrono::duration_cast<std::chrono::nanoseconds>(control_timeout).count()),
      manager_(CanManager::get_instance(manager_name_)) {
  const int64_t t = now_ns();
  last_rx_ns_.store(t, std::memory_order_relaxed);
  last_control_ns_.store(t, std::memory_order_relaxed);
}

void CanDevice::register_with_manager(const std::shared_ptr<CanDevice>& device) {
  if (!device) {
    throw std::invalid_argument("Cannot register a null CAN device");
  }
  if (device->name().empty()) {
    throw std::invalid_argument("CAN device name cannot be empty");
  }
  if (device->manager_name().empty()) {
    throw std::invalid_argument("CAN manager name cannot be empty");
  }
  if (!device->manager_->register_device(device)) {
    throw_registration_error(device);
  }
}

void CanDevice::throw_registration_error(const std::shared_ptr<CanDevice>& device) {
  for (const auto& existing : device->manager_->get_all_devices()) {
    if (existing->name() == device->name()) {
      throw std::runtime_error(
          "CAN device name '" + device->name() + "' is already registered");
    }
    if (existing->id() == device->id()) {
      throw std::runtime_error(
          "CAN device ID " + std::to_string(device->id()) + " is already registered");
    }
    if (device->filter().overlaps(existing->filter())) {
      throw std::runtime_error(
          "CAN filter for device '" + device->name() + "' overlaps device '" +
          existing->name() + "'");
    }
  }
  throw std::runtime_error("Failed to register CAN device '" + device->name() + "'");
}

std::shared_ptr<CanDevice> CanDevice::find_registered_device(
    const std::optional<std::string>& name,
    const std::optional<uint32_t>& id,
    const std::string& manager_name) {
  if ((!name || name->empty()) && !id) {
    throw std::invalid_argument("Find a CAN device by name, ID, or both");
  }
  if (manager_name.empty()) {
    throw std::invalid_argument("CAN manager name cannot be empty");
  }

  const auto manager = CanManager::get_instance(manager_name);
  std::shared_ptr<CanDevice> by_name;
  std::shared_ptr<CanDevice> by_id;

  if (name && !name->empty()) {
    by_name = manager->get_device(*name);
    if (!by_name) {
      throw std::runtime_error("No CAN device named '" + *name +
                               "' is registered on '" + manager_name + "'");
    }
  }
  if (id) {
    by_id = manager->get_device(*id);
    if (!by_id) {
      throw std::runtime_error("No CAN device with ID " + std::to_string(*id) +
                               " is registered on '" + manager_name + "'");
    }
  }
  if (by_name && by_id && by_name != by_id) {
    throw std::runtime_error("The requested CAN device name and ID identify different devices");
  }
  return by_name ? by_name : by_id;
}

CanDevice::~CanDevice() {
  if (registered_.exchange(false, std::memory_order_acq_rel)) {
    manager_->deregister_device(name_, this);
  }

  for (auto& msg : periodic_messages_) {
    if (msg) {
      msg->cancel();
    }
  }
  for (const auto& svc : services_) {
    svc->cancel();
  }
}

int64_t CanDevice::now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::shared_ptr<PeriodicMessage> CanDevice::create_periodic(
    std::chrono::microseconds period,
    PeriodicMessage::FrameBuilder normal_builder,
    PeriodicMessage::FrameBuilder safe_builder) {
  // weak_from_this() is empty during base/derived constructors; bound in CanManager::register_device()
  auto msg = std::make_shared<PeriodicMessage>(
      std::weak_ptr<CanDevice>{},
      period,
      std::move(normal_builder),
      std::move(safe_builder),
      std::chrono::steady_clock::now());

  periodic_messages_.push_back(msg);
  return msg;
}

std::vector<std::shared_ptr<TxMessage>> CanDevice::bind_and_collect_periodic_messages() {
  const auto start_tp = std::chrono::steady_clock::now();
  const auto self_weak = weak_from_this();

  std::vector<std::shared_ptr<TxMessage>> out;
  out.reserve(periodic_messages_.size());

  for (const auto& msg : periodic_messages_) {
    msg->bind(self_weak, start_tp);
    out.push_back(msg);
  }
  return out;
}

std::shared_ptr<ServiceChannel> CanDevice::create_service(ServiceChannel::AckMatcher matcher) {
  auto service = std::shared_ptr<ServiceChannel>(new ServiceChannel(*this, std::move(matcher)));
  services_.push_back(service);
  return service;
}

ImmediateCommand CanDevice::create_immediate(ImmediateCommand::FrameBuilder builder) {
  return ImmediateCommand(*this, std::move(builder));
}

void CanDevice::mark_control_updated() {
  last_control_ns_.store(now_ns(), std::memory_order_release);

  DeviceState expected = DeviceState::SOFT_BLOCK;
  if (state_.compare_exchange_strong(
          expected, DeviceState::NORMAL, std::memory_order_acq_rel)) {
    on_normal_trigger();
  }
}

void CanDevice::handle_rx(const CanFrame& frame) {
  const int64_t t = now_ns();
  last_rx_ns_.store(t, std::memory_order_release);

  if (state_.load(std::memory_order_acquire) == DeviceState::ERROR_HARD_BLOCK) {
    const uint8_t count = recovery_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (count >= kRecoveryFramesRequired) {
      recovery_count_.store(0, std::memory_order_release);
      const bool ctrl_stale =
          (control_timeout_ns_ > 0) &&
          ((t - last_control_ns_.load(std::memory_order_acquire)) > control_timeout_ns_);
      const DeviceState next_state =
          ctrl_stale ? DeviceState::SOFT_BLOCK : DeviceState::NORMAL;
      const DeviceState prev_state = state_.exchange(next_state, std::memory_order_acq_rel);
      if (next_state == DeviceState::SOFT_BLOCK && prev_state != DeviceState::SOFT_BLOCK) {
        on_soft_block_trigger();
      } else if (next_state == DeviceState::NORMAL && prev_state != DeviceState::NORMAL) {
        on_normal_trigger();
      }
    }
  }

  for (const auto& svc : services_) {
    if (svc->try_match_ack(frame)) {
      break;
    }
  }

  on_rx(frame);
}

void CanDevice::check_watchdog(int64_t now) {
  if (rx_timeout_ns_ > 0 &&
      (now - last_rx_ns_.load(std::memory_order_acquire)) > rx_timeout_ns_) {
    recovery_count_.store(0, std::memory_order_release);
    const DeviceState prev =
        state_.exchange(DeviceState::ERROR_HARD_BLOCK, std::memory_order_acq_rel);
    if (prev != DeviceState::ERROR_HARD_BLOCK) {
      on_hard_block_trigger();
    }
    return;
  }

  if (control_timeout_ns_ > 0 &&
      state_.load(std::memory_order_acquire) == DeviceState::NORMAL &&
      (now - last_control_ns_.load(std::memory_order_acquire)) > control_timeout_ns_) {
    DeviceState expected = DeviceState::NORMAL;
    if (state_.compare_exchange_strong(
            expected, DeviceState::SOFT_BLOCK, std::memory_order_acq_rel)) {
      on_soft_block_trigger();
    }
  }
}