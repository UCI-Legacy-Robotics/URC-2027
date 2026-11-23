#pragma once

#include <array>
#include <cmath>
#include <cstddef>

// ROS-independent trigger/state logic, also exercised by the offline tests.
class GripperControl
{
public:
  void stop()
  {
    armed_ = false;
    direction_ = 0;
  }

  void read_triggers(double open, double close, double threshold)
  {
    if (!std::isfinite(open) || !std::isfinite(close)) {
      stop();
      return;
    }
    // Released triggers report +1. Zero/uninitialized axes must not arm motion.
    if (open > threshold && close > threshold) {
      armed_ = true;
    }
    direction_ = armed_ ? static_cast<int>(close < -threshold) -
                         static_cast<int>(open < -threshold) : 0;
  }

  std::array<double, 2> command(const std::array<double, 2>& positions,
                               bool fresh_input, bool fresh_state,
                               double lower, double upper, double speed)
  {
    std::array<double, 2> velocities{};
    if (!fresh_input || !fresh_state || !std::isfinite(positions[0]) ||
        !std::isfinite(positions[1])) {
      stop();
      return velocities;
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
      const bool at_limit = direction_ > 0 ? positions[i] >= upper - 1e-5 :
                                           positions[i] <= lower + 1e-5;
      if (!at_limit) {
        velocities[i] = direction_ * speed;
      }
    }
    return velocities;
  }

private:
  bool armed_ = false;
  int direction_ = 0;
};
