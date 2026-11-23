#include "gripper_control.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace std::chrono_literals;
constexpr double speed = 0.015;

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect(const std::array<double, 2>& actual, const std::array<double, 2>& expected)
{
  require(std::abs(actual[0] - expected[0]) < 1e-12 &&
          std::abs(actual[1] - expected[1]) < 1e-12, "Unexpected gripper velocity");
}

void test_triggers_and_stops()
{
  GripperControl gripper;
  auto command = [&](std::array<double, 2> positions = {0.015, 0.015},
                     bool input = true, bool state = true) {
    return gripper.command(positions, input, state, 0.0, 0.03, speed);
  };
  // Held/uninitialized triggers cannot arm on startup.
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
  gripper.read_triggers(0, 0, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
  gripper.read_triggers(1, 1, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {speed, speed});
  // A release or conflicting press must zero BOTH fingers immediately and stay zero.
  for (auto triggers : {std::array<double, 2>{1, 1}, {-1, -1}, {0, 0}}) {
    gripper.read_triggers(triggers[0], triggers[1], 0.5);
    for (int i = 0; i < 100; ++i) {
      expect(command(), {0, 0});
    }
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {speed, speed});
  }
  // Lost input and stale feedback stop and require release even after recovery.
  for (bool stale_input : {true, false}) {
    expect(command({0.015, 0.015}, !stale_input, stale_input), {0, 0});
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {0, 0});
    gripper.read_triggers(1, 1, 0.5);
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {speed, speed});
  }
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), 2.0, -2.0}) {
    gripper.read_triggers(invalid, -1, 0.5);
    expect(command(), {0, 0});
    gripper.read_triggers(1, 1, 0.5);
    gripper.read_triggers(1, -1, 0.5);
    expect(command(), {speed, speed});
  }
  expect(command({0, std::numeric_limits<double>::quiet_NaN()}), {0, 0});
  gripper.read_triggers(1, -1, 0.5);
  expect(command(), {0, 0});
}

void test_limits_and_reversal()
{
  GripperControl gripper;
  auto command = [&](std::array<double, 2> positions) {
    return gripper.command(positions, true, true, 0.0, 0.03, speed);
  };
  gripper.read_triggers(1, 1, 0.5);
  gripper.read_triggers(1, -1, 0.5);
  expect(command({0.029, 0.015}), {0, speed});
  // A held trigger must not restart a stopped finger due to feedback jitter.
  expect(command({0.0285, 0.015}), {0, speed});
  gripper.read_triggers(1, -1, 0.5);  // Autorepeat must not reset the latch.
  expect(command({0.0285, 0.015}), {0, speed});
  // Reversing requires no release and works even beyond the configured limits.
  gripper.read_triggers(-1, 1, 0.5);
  expect(command({0.04, 0.04}), {-speed, -speed});
  expect(command({0.001, 0.015}), {0, -speed});
  expect(command({0.0015, 0.015}), {0, -speed});
  gripper.read_triggers(1, -1, 0.5);
  expect(command({-0.001, -0.001}), {speed, speed});
  // Taper toward BOTH endpoints; the other finger can still travel at full speed.
  const auto close = command({0.024, 0.015});
  expect(close, {speed / 2, speed});
  gripper.read_triggers(-1, 1, 0.5);
  expect(command({0.006, 0.015}), {-speed / 2, -speed});
  // Previously this q produced -0.02, crossing the hard stop in one 20ms tick.
  expect(command({0.0002, 0.0002}), {0, 0});
}

void test_delayed_feedback_cycles()
{
  // Kinematic integration only, NOT a Gazebo dynamics simulation. Feedback is
  // sampled every 80ms while commands run at 50Hz, including endpoint holds.
  GripperControl gripper;
  std::array<double, 2> q{0.002, 0.003};
  auto measured = q;
  gripper.read_triggers(1, 1, 0.5);
  for (int cycle = 0; cycle < 20; ++cycle) {
    const int direction = cycle % 2 == 0 ? 1 : -1;
    gripper.read_triggers(direction, -direction, 0.5);
    std::array<double, 2> velocity{};
    for (int tick = 0; tick < 500; ++tick) {
      if (tick % 4 == 0) {
        measured = q;
      }
      gripper.read_triggers(direction, -direction, 0.5);
      velocity = gripper.command(measured, true, true, 0, 0.03, speed);
      for (std::size_t i = 0; i < q.size(); ++i) {
        q[i] += 0.02 * velocity[i];
        require(q[i] >= 0.001 && q[i] <= 0.029, "Crossed guarded endpoint");
        require(std::abs(velocity[i]) <= speed, "Exceeded configured speed");
      }
    }
    expect(velocity, {0, 0});
    for (double position : q) {
      require(direction > 0 ? position > 0.0288 : position < 0.0012,
              "Failed to reach endpoint");
    }
    gripper.read_triggers(1, 1, 0.5);
    expect(gripper.command(q, true, true, 0, 0.03, speed), {0, 0});
  }
}

void test_partial_feedback()
{
  GripperFeedback feedback;
  const auto start = GripperFeedback::Clock::now();
  require(!feedback.fresh(start, 0.5), "Uninitialized feedback was fresh");
  feedback.update({"claw_to_left_grip"}, {0.01}, start);
  require(!feedback.fresh(start, 0.5), "Missing finger was accepted");
  feedback.update({"claw_to_right_grip"}, {0.02}, start + 100ms);
  require(feedback.fresh(start + 100ms, 0.5), "Split joint feedback was rejected");
  expect(feedback.positions(), {0.01, 0.02});
  feedback.update({"shoulder_to_elbow"}, {0}, start + 200ms);
  require(feedback.fresh(start + 200ms, 0.5), "Arm-only message invalidated fingers");
  feedback.update({"claw_to_right_grip"}, {0.02}, start + 600ms);
  require(!feedback.fresh(start + 600ms, 0.5), "New right finger masked stale left finger");
  feedback.update({"claw_to_left_grip"}, {0.01}, start + 600ms);
  require(feedback.fresh(start + 600ms, 0.5), "Recovery failed");
  feedback.update({"claw_to_left_grip"}, {}, start + 600ms);
  require(!feedback.fresh(start + 600ms, 0.5), "Malformed position array was accepted");
  feedback.update({"claw_to_right_grip", "claw_to_left_grip"}, {0.025, 0.015}, start + 700ms);
  expect(feedback.positions(), {0.015, 0.025});
  feedback.update({"claw_to_left_grip"}, {std::numeric_limits<double>::quiet_NaN()}, start + 700ms);
  require(!feedback.fresh(start + 700ms, 0.5), "NaN position was accepted");
}

void test_frozen_feedback_near_limits()
{
  for (int direction : {-1, 1}) {
    GripperControl gripper;
    gripper.read_triggers(1, 1, 0.5);
    gripper.read_triggers(direction, -direction, 0.5);
    const double measured = direction > 0 ? 0.027 : 0.003;
    double actual = measured;
    for (int tick = 0; tick < 40; ++tick) {
      // State stalls while /joy remains live. Stop at the 0.5s watchdog;
      // the slowdown zone must cover the possible travel before it expires.
      const auto velocity = gripper.command({measured, measured}, true, tick <= 25,
                                           0, 0.03, speed);
      actual += velocity[0] * 0.02;
      require(actual >= 0.001 && actual <= 0.029, "Frozen feedback crossed endpoint");
      if (tick > 25) {
        expect(velocity, {0, 0});
      }
    }
  }
}

int main()
{
  test_triggers_and_stops();
  test_limits_and_reversal();
  test_delayed_feedback_cycles();
  test_partial_feedback();
  test_frozen_feedback_near_limits();
  std::cout << "PASS: release/conflict stops, watchdog/rearm, guarded limits, reversal, "
               "endpoint jitter, 20 delayed-feedback cycles, partial/stale/invalid feedback\n";
}
