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
  /// @brief Creates a CAN device and initializes its watchdog timestamps.
  /// @param name Unique name used to register the device.
  /// @param filter Receive filter used to route matching CAN frames to this device.
  /// @param rx_timeout Time without received frames before entering hard-block; non-positive disables it.
  /// @param control_timeout Time without updated control before entering soft-block; non-positive disables it.
  /// @param manager_name Name of the CAN manager that owns this device.
  /// @param id Device ID; defaults to the ID in @p filter.
  CanDevice(
      std::string name,
      CanFilter filter,
      std::chrono::milliseconds rx_timeout = std::chrono::milliseconds(1000),
      std::chrono::milliseconds control_timeout = std::chrono::milliseconds(1000),
      std::string manager_name = "can0",
      std::optional<uint32_t> id = std::nullopt);

  virtual ~CanDevice();

  /// @brief Gets the registered name of this device.
  /// @return The device name.
  const std::string& name() const noexcept { return name_; }

  /// @brief Gets the name of the CAN manager that owns this device.
  /// @return The manager name.
  const std::string& manager_name() const noexcept { return manager_name_; }

  /// @brief Gets this device's CAN ID.
  /// @return The device ID.
  uint32_t id() const noexcept { return id_; }

  /// @brief Gets the receive filter used by this device.
  /// @return The device's CAN filter.
  CanFilter filter() const noexcept { return filter_; }

  /// @brief Gets the current watchdog state.
  /// @return NORMAL, SOFT_BLOCK, or ERROR_HARD_BLOCK.
  DeviceState state() const noexcept { return state_.load(std::memory_order_acquire); }

  /// @brief Checks whether the device is not hard-blocked.
  /// @return False only when the device is in ERROR_HARD_BLOCK.
  bool is_healthy() const noexcept { return state() != DeviceState::ERROR_HARD_BLOCK; }

protected:
  // ------------- Functions implemented by derived CAN Device -----------------

  /// @brief Processes a CAN frame routed to this device.
  // Generally intended for extracting frame data and updating atomic variable in derived CanDevice class
  /// @param frame The received frame.
  virtual void on_rx(const CanFrame& frame) = 0;

  /// @brief Runs when the device transitions into NORMAL.
  /// Default behavior does nothing, override as necessary
  virtual void on_normal_trigger() {}

  /// @brief Runs when the device transitions into SOFT_BLOCK.
  /// Default behavior does nothing, override as necessary
  virtual void on_soft_block_trigger() {}

  /// @brief Runs when the device transitions into ERROR_HARD_BLOCK.
  /// Default behavior does nothing, override as necessary
  virtual void on_hard_block_trigger() {}
  
  // ------------- Helper functions to use in derived CAN Device -----------------

  /// @brief Factory helper to create a periodic message in derived CanDevice constructor
  /// @param period Interval between when message will reschedule itself to run.
  /// @param normal_builder Builds a frame while the device is NORMAL.
  /// @param safe_builder Builds a frame while the device is SOFT_BLOCK; may be empty.
  /// @return The periodic message, which is queued upon device registration
  std::shared_ptr<PeriodicMessage> create_periodic(
      std::chrono::microseconds period,
      PeriodicMessage::FrameBuilder normal_builder,
      PeriodicMessage::FrameBuilder safe_builder = nullptr);

  /// @brief Factory helper to create a one-shot message in derived CanDevice constructor
  /// @param builder Builds the CAN frame when the command is sent.
  /// @return The immediate command.
  ImmediateCommand create_immediate(ImmediateCommand::FrameBuilder builder);

  /// @brief Factory helper to create a service message in derived CanDevice constructor
  /// @param matcher Function which returns true when a received frame matches a service repsonse
  /// @return The service channel.
  std::shared_ptr<ServiceChannel> create_service(ServiceChannel::AckMatcher matcher);

  /// @brief Records a fresh control timestamp and clears `SOFT_BLOCK` if active.
  /// @warning Must be called in every control set function in derived CanDevice class
  // or else `SOFT_BLOCK` will continuously be set
  /// (e.g., `set_vel()`, `set_pos()`, `set_torque()`).
  void mark_control_updated();

  // ------------- Functions only used by Can Device Factory wrapper class ----------------- 

  static void register_with_manager(const std::shared_ptr<CanDevice>& device);

  static std::shared_ptr<CanDevice> find_registered_device(
      const std::optional<std::string>& name,
      const std::optional<uint32_t>& id,
      const std::string& manager_name);

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
  /// @brief Constructs and registers a Can Device.
  /// Refer to device specific constructor to determine order of arguments to pass in
  /// @warning MAKE SURE CAN ID AND DEVICE NAME ARE UNIQUE
  /// @return The registered device.
  /// @throws std::invalid_argument or std::runtime_error if registration fails.
  template <typename... Args>
  static std::shared_ptr<Derived> register_device(Args&&... args) {
    auto device = std::shared_ptr<Derived>(new Derived(std::forward<Args>(args)...));
    register_with_manager(device);
    return device;
  }

  /// @brief Finds a registered device of type @p Derived by name.
  /// @param name Registered device name.
  /// @param manager_name CAN manager to search; defaults to "can0".
  /// @return The matching device.
  /// @throws std::runtime_error If the device is missing or has a different derived type.
  static std::shared_ptr<Derived> find_device(
      const std::string& name,
      const std::string& manager_name = "can0") {
    return find_device_impl(name, std::nullopt, manager_name);
  }

  /// @brief Finds a registered device of type @p Derived by CAN ID.
  /// @param id Registered device ID.
  /// @param manager_name CAN manager to search; defaults to "can0".
  /// @return The matching device.
  /// @throws std::runtime_error If the device is missing or has a different derived type.
  static std::shared_ptr<Derived> find_device(
      uint32_t id,
      const std::string& manager_name = "can0") {
    return find_device_impl(std::nullopt, id, manager_name);
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
