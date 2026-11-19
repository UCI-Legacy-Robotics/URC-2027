#ifndef YELLOWJACKET_HARDWARE__ENCODER_UNITS_HPP_
#define YELLOWJACKET_HARDWARE__ENCODER_UNITS_HPP_

#include <cmath>
#include <stdexcept>

namespace yellowjacket_hardware
{
// N includes encoder decoding and every gear reduction between encoder and wheel.
// These conversions also apply to counts/s <-> rad/s. Handle direction separately.
class EncoderUnits
{
public:
  explicit EncoderUnits(double counts_per_wheel_revolution)
  : counts_per_revolution_(counts_per_wheel_revolution)
  {
    if (!std::isfinite(counts_per_revolution_) || counts_per_revolution_ <= 0.0) {
      throw std::invalid_argument("Counts per wheel revolution must be finite and positive");
    }
  }

  double to_radians(double counts) const {return counts * tau_ / counts_per_revolution_;}
  // Round and check the RoboClaw integer range before sending this value to hardware.
  double to_counts(double radians) const {return radians * counts_per_revolution_ / tau_;}

private:
  static constexpr double tau_ = 6.28318530717958647692;
  double counts_per_revolution_;
};
}  // namespace yellowjacket_hardware
#endif
