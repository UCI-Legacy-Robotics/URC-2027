#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

// JointState publishers may send subsets. Track each finger's age separately;
// an arm-only message must neither invalidate nor refresh the gripper feedback.
class GripperFeedback
{
public:
  using Clock = std::chrono::steady_clock;

  void update(const std::vector<std::string>& names, const std::vector<double>& positions,
              Clock::time_point time)
  {
    for (std::size_t i = 0; i < joints_.size(); ++i) {
      const auto joint = std::find(names.begin(), names.end(), joints_[i]);
      if (joint == names.end()) {
        continue;
      }
      const auto index = static_cast<std::size_t>(joint - names.begin());
      positions_[i] = index < positions.size() ? positions[index] :
          std::numeric_limits<double>::quiet_NaN();
      received_[i] = time;
      valid_[i] = std::isfinite(positions_[i]);
    }
  }

  bool fresh(Clock::time_point time, double timeout) const
  {
    for (std::size_t i = 0; i < joints_.size(); ++i) {
      if (!valid_[i] || std::chrono::duration<double>(time - received_[i]).count() > timeout) {
        return false;
      }
    }
    return true;
  }

  const std::array<double, 2>& positions() const { return positions_; }

private:
  const std::array<std::string, 2> joints_{"claw_to_left_grip", "claw_to_right_grip"};
  std::array<double, 2> positions_{};
  std::array<Clock::time_point, 2> received_{};
  std::array<bool, 2> valid_{};
};

// ROS-independent trigger/state logic, also exercised by the offline tests.
class GripperControl
{
public:
  void stop()
  {
    armed_ = false;
    set_direction(0);
  }

  void read_triggers(double open, double close, double threshold)
  {
    if (!std::isfinite(open) || !std::isfinite(close) ||
        std::abs(open) > 1.0 || std::abs(close) > 1.0) {
      stop();
      return;
    }
    // Released triggers report +1. Zero/uninitialized axes must not arm motion.
    if (open > threshold && close > threshold) {
      armed_ = true;
    }
    set_direction(armed_ ? static_cast<int>(close < -threshold) -
                           static_cast<int>(open < -threshold) : 0);
  }

  std::array<double, 2> command(const std::array<double, 2>& positions,
                               bool fresh_input, bool fresh_state,
                               double lower, double upper, double speed,
                               double margin = 0.001, double slowdown_distance = 0.01)
  {
    std::array<double, 2> velocities{};
    if (!fresh_input || !fresh_state || !std::isfinite(positions[0]) ||
        !std::isfinite(positions[1])) {
      stop();
      return velocities;
    }
    if (direction_ == 0) {
      return velocities;
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
      // Only limit motion INTO the endpoint. Reversal always clears the latch,
      // including when a finger has overshot the configured travel interval.
      const double remaining = direction_ > 0 ? upper - margin - positions[i] :
                                                positions[i] - (lower + margin);
      if (remaining <= 0.0001) {
        at_limit_[i] = true;
      }
      if (!at_limit_[i]) {
        velocities[i] = direction_ * speed * std::clamp(remaining / slowdown_distance, 0.0, 1.0);
      }
    }
    return velocities;
  }

private:
  void set_direction(int direction)
  {
    if (direction != direction_) {
      at_limit_.fill(false);
    }
    direction_ = direction;
  }

  bool armed_ = false;
  int direction_ = 0;
  std::array<bool, 2> at_limit_{};
};
