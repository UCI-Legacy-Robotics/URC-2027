#include "embedded/devices/StepperDevice.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// This test program demonstrates the usage of the StepperDevice class by simulating a control loop
// with a mock Stepper device. It registers an Stepper device, sends control commands,
// and retrieves telemetry data in a loop, printing the results to the console.

using namespace std::chrono_literals;

int main(int argc, char** argv) {
  const std::string interface_name = (argc > 1) ? argv[1] : "vcan0";

  auto stepper = StepperDevice::register_device(
      "stepper_device", /*node_id=*/0x23, /*rx_timeout=*/300ms, /*control_timeout=*/200ms, interface_name);
  stepper = StepperDevice::find_device("stepper_device", interface_name);

  std::cout << "Registered '" << stepper->name() << "' on " << interface_name << "\n";

  int32_t position = 0;
  int32_t velocity = 0;


  // 4. Run a 100 Hz control & telemetry loop demonstrating vel, pos, and torque modes
  for (int step = 0; step < 1000; ++step) {
    if (step < 500) {
      stepper->set_pos(1000);      // First 25.0s: Velocity control (20 steps/s)
    } else {
      stepper->set_vel(30);     // Next 25.0s:  Position control (30 steps)
    } 

    position = stepper->get_pos();
    velocity = stepper->get_vel();

    if (step % 25 == 0) {
      std::cout << "Step " << step
                << " | Pos: " << position
                << " | Vel: " << velocity
                << " | Healthy: " << (stepper->is_healthy() ? "YES" : "NO") << "\n";
    }

    std::this_thread::sleep_for(10ms);
  }

  stepper->set_vel(20);  
  return 0;
}