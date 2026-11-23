#include "gripper_control.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

void expect(const std::array<double, 2>& actual, const std::array<double, 2>& expected)
{
  if (actual != expected) {
    throw std::runtime_error("Unexpected gripper velocity");
  }
}

int main()
{
  GripperControl gripper;
  auto command = [&](std::array<double, 2> positions = {0.01, 0.01},
                     bool input = true, bool state = true) {
    return gripper.command(positions, input, state, 0.0, 0.03, 0.02);
  };
  // A held trigger on startup must not move; zero axes must not arm either.
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
  gripper.read_triggers(0, 0, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
  gripper.read_triggers(1, 1, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0.02, 0.02});
  expect(command({0.03, 0.02}), {0, 0.02});
  gripper.read_triggers(-1, 1, 0.5);
  expect(command({0, 0.01}), {0, -0.02});
  expect(command({0.04, 0.04}), {-0.02, -0.02});
  gripper.read_triggers(-1, -1, 0.5);
  expect(command(), {0, 0});
  // Lost input and stale feedback both stop and require a new trigger release.
  for (bool stale_input : {true, false}) {
    gripper.read_triggers(1, -1, 0.5);
    expect(command({0.01, 0.01}, !stale_input, stale_input), {0, 0});
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {0, 0});
    gripper.read_triggers(1, 1, 0.5);
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {0.02, 0.02});
  }
  gripper.read_triggers(std::numeric_limits<double>::quiet_NaN(), -1, 0.5);
  expect(command(), {0, 0});
  gripper.read_triggers(1, 1, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command({0, std::numeric_limits<double>::quiet_NaN()}), {0, 0});
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
  std::cout << "PASS: trigger arming, both-trigger stop, independent limits, stale input/state, NaNs\n";
}
