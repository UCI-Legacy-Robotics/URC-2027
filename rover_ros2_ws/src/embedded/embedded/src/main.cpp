#include "embedded/core/CanManager.hpp"
#include "embedded/devices/ODriveDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char** argv) {
  const std::string interface_name = (argc > 1) ? argv[1] : "vcan0";

  auto can_mgr = CanManager::get_instance(interface_name);
  can_mgr->get_or_register_device<ODriveDevice>("wheel_motor", /*node_id=*/1, /*rx_timeout=*/0ms, /*control_timeout=*/0ms);

  auto odrive = can_mgr->get_or_register_device<ODriveDevice>("wheel_motor", /*node_id=*/1, /*rx_timeout=*/0ms, /*control_timeout=*/0ms);

  std::cout << "Registered '" << odrive->name() << "' on " << interface_name << "\n";

  // 2. Send immediate message: zero the encoder origin
  if (odrive->set_origin(0.0f)) {
    std::cout << "[Immediate] Sent set_origin(0.0)\n";
  }

  // 3. Send service message with ACK: put ODrive into CLOSED_LOOP_CONTROL (8)
  std::cout << "[Service] Requesting CLOSED_LOOP_CONTROL...\n";
  if (odrive->set_axis(ODriveDevice::kAxisStateClosedLoop)) {
    while (odrive->axis_service_state() == ServiceState::WAITING) {
      std::this_thread::sleep_for(10ms);
    }

    if (odrive->axis_service_state() == ServiceState::RECEIVED) {
      std::cout << "[Service] ACK received! Axis is in CLOSED_LOOP_CONTROL.\n";
    } else {
      std::cout << "[Service] Timed out waiting for ODrive heartbeat ACK.\n";
    }
  }

  // 4. Run a 50 Hz control & telemetry loop demonstrating vel, pos, and torque modes
  for (int step = 0; step < 1000; ++step) {
    if (step < 333) {
      odrive->set_vel(2.5f);      // First 1.0s: Velocity control (2.5 rev/s)
    } else if (step < 666) {
      odrive->set_pos(10.0f);     // Next 1.0s:  Position control (10.0 rev)
    } else {
      odrive->set_torque(0.4f);   // Last 1.0s:  Torque control (0.4 Nm)
    }

    if (step % 25 == 0) {
      std::cout << "Step " << step
                << " | Pos: " << odrive->get_pos()
                << " | Vel: " << odrive->get_vel()
                << " | Torque: " << odrive->get_torque()
                << " | Healthy: " << (odrive->is_healthy() ? "YES" : "NO") << "\n";
    }

    std::this_thread::sleep_for(20ms);
  }

  // 5. Return to IDLE before shutting down
  odrive->set_axis(ODriveDevice::kAxisStateIdle);
  std::this_thread::sleep_for(50ms);

  can_mgr->stop();
  std::cout << "Shutdown complete.\n";
  return 0;
}