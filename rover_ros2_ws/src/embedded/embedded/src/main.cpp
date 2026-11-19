#include "embedded/devices/ODriveDevice.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char** argv) {
  const std::string interface_name = (argc > 1) ? argv[1] : "vcan0";

  auto odrive = ODriveDevice::register_device(
      "wheel_motor", /*node_id=*/1, /*rx_timeout=*/0ms, /*control_timeout=*/0ms, interface_name);
  odrive = ODriveDevice::find_device("wheel_motor", interface_name);

  std::array<std::shared_ptr<ODriveDevice>, 10> odrives{};
  odrives[0] = odrive;
  for (std::size_t i = 1; i < odrives.size(); ++i) {
    const auto node_id = static_cast<uint8_t>(i + 1);
    odrives[i] = ODriveDevice::register_device(
        "wheel_motor_" + std::to_string(node_id), node_id, 0ms, 100ms, interface_name);
  }

  std::this_thread::sleep_for(10000ms); // Wait for my caveman brain to process ts

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

  std::array<float, 10> positions{};
  std::array<float, 10> velocities{};
  std::array<float, 10> torques{};

  // 4. Run a 100 Hz control & telemetry loop demonstrating vel, pos, and torque modes
  for (int step = 0; step < 10000; ++step) {
    if (step < 3333) {
      odrive->set_vel(2.5f);      // First 1.0s: Velocity control (2.5 rev/s)
    } else if (step < 6666) {
      odrive->set_pos(10.0f);     // Next 1.0s:  Position control (10.0 rev)
    } else {
      odrive->set_torque(0.4f);   // Last 1.0s:  Torque control (0.4 Nm)
    }

    for (std::size_t i = 0; i < odrives.size(); ++i) {
      if (i != 0) {
        odrives[i]->set_vel(2.5f);
      }
      positions[i] = odrives[i]->get_pos();
      velocities[i] = odrives[i]->get_vel();
      torques[i] = odrives[i]->get_torque();
    }

    if (step % 25 == 0) {
      std::cout << "Step " << step
                << " | Pos: " << positions[0]
                << " | Vel: " << velocities[0]
                << " | Torque: " << torques[0]
                << " | Node 10 Pos: " << positions.back()
                << " | Node 10 Vel: " << velocities.back()
                << " | Healthy: " << (odrive->is_healthy() ? "YES" : "NO") << "\n";
    }

    std::this_thread::sleep_for(10ms);
  }

  // 5. Return to IDLE before shutting down
  odrive->set_axis(ODriveDevice::kAxisStateIdle);
  std::this_thread::sleep_for(50ms);

  std::cout << "Shutdown complete.\n";
  return 0;
}