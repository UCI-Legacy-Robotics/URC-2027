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
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(0),
      std::string manager_name = "can0",
      std::optional<uint32_t> id = std::nullopt);

  virtual ~CanDevice();

  const std::string& name() const noexcept { return name_; }
  const std::string& manager_name() const noexcept { return manager_name_; }
  uint32_t id() const noexcept { return id_; }
  CanFilter filter() const noexcept { return filter_; }
  DeviceState state() const noexcept { return state_.load(std::memory_order_acquire); }
  bool is_healthy() const noexcept { return state() != DeviceState::ERROR_HARD_BLOCK; }

protected:
  // --- Device Subclass Overrides ---
  virtual void on_rx(const CanFrame& frame) = 0;
  virtual void on_soft_block() {}
  virtual void on_hard_block() {}

  static void register_with_manager(const std::shared_ptr<CanDevice>& device);
  static std::shared_ptr<CanDevice> find_registered_device(
      const std::optional<std::string>& name,
      const std::optional<uint32_t>& id,
      const std::string& manager_name);

  // --- Device Subclass Factories for Member Initialization ---
  // Creates a two-lambda PeriodicMessage (normal + soft-block degraded) queued upon registration
  std::shared_ptr<PeriodicMessage> create_periodic(
      std::chrono::microseconds period,
      PeriodicMessage::FrameBuilder normal_builder,
      PeriodicMessage::FrameBuilder safe_builder = nullptr);

  ImmediateCommand create_immediate(ImmediateCommand::FrameBuilder builder);

  // Create persistent message helpers during derived-device construction.
  std::shared_ptr<ServiceChannel> create_service(ServiceChannel::AckMatcher matcher);

  void mark_control_updated() noexcept;

private:
  friend class CanManager;
  friend class ServiceChannel;
  friend class ImmediateCommand;

  std::vector<std::shared_ptr<TxMessage>> bind_and_collect_periodic_messages();
  [[noreturn]] static void throw_registration_error(
      const std::shared_ptr<CanDevice>& device);
  static std::shared_ptr<CanDevice> find_instance(
      const std::optional<std::string>& name,
      const std::optional<uint32_t>& id,
      const std::string& manager_name);
  void handle_rx(const CanFrame& frame);
  void check_watchdog(int64_t now_ns);
  static int64_t now_ns() noexcept;

  const std::string name_;
  const std::string manager_name_;
  const CanFilter filter_;
  const uint32_t id_;
  const int64_t rx_timeout_ns_;
  const int64_t control_timeout_ns_;

  std::atomic<DeviceState> state_{DeviceState::NORMAL};
  std::atomic<int64_t> last_rx_ns_{0};
  std::atomic<int64_t> last_control_ns_{0};
  std::atomic<uint8_t> recovery_count_{0};

  std::shared_ptr<CanManager> manager_;
  std::atomic<bool> registered_{false};
  std::vector<std::shared_ptr<PeriodicMessage>> periodic_messages_;
  std::vector<std::shared_ptr<ServiceChannel>> services_;
};

template <typename Derived>
class CanDeviceFactory : public CanDevice {
protected:
  using CanDevice::CanDevice;

public:
  template <typename... Args>
  static std::shared_ptr<Derived> register_device(Args&&... args) {
    auto device = std::shared_ptr<Derived>(new Derived(std::forward<Args>(args)...));
    register_with_manager(device);
    return device;
  }

  static std::shared_ptr<Derived> find_device(
      const std::string& name,
      const std::string& manager_name = "can0") {
    return find_device_impl(name, std::nullopt, manager_name);
  }

  static std::shared_ptr<Derived> find_device(
      uint32_t id,
      const std::string& manager_name = "can0") {
    return find_device_impl(std::nullopt, id, manager_name);
  }

  static std::shared_ptr<Derived> find_device() {
    return find_device_impl(std::nullopt, std::nullopt, "can0");
  }

  static std::shared_ptr<Derived> find_device(
      const std::optional<std::string>& name,
      const std::optional<uint32_t>& id,
      const std::string& manager_name = "can0") {
    return find_device_impl(name, id, manager_name);
  }

private:
  static std::shared_ptr<Derived> find_device_impl(
      const std::optional<std::string>& name,
      const std::optional<uint32_t>& id,
      const std::string& manager_name) {
    auto device = find_registered_device(name, id, manager_name);
    auto typed_device = std::dynamic_pointer_cast<Derived>(device);
    if (!typed_device) {
      if (name && !name->empty()) {
        throw std::runtime_error(
            "CAN device named '" + *name + "' has a different device type");
      }
      if (id) {
        throw std::runtime_error(
            "CAN device with ID " + std::to_string(*id) + " has a different device type");
      }
      throw std::runtime_error("Found CAN device has a different device type");
    }
    return typed_device;
  }
};
