// Filters.h - turning noisy raw readings into the flags the safety rules use.
// All three filters are ported from the SensorsManager class of the sketch.
#pragma once
#include <stdint.h>

#include "Config.h"

namespace scooter {

// IR obstacle module (digital, active low). A moving average over the last
// 10 samples (200 ms at 50 Hz) stops a single flicker from stopping the ride.
class IrFilter {
 public:
  IrFilter();
  // rawLevel: the pin level, 1 = nothing in front, 0 = reflection seen.
  // Returns true when more than half of the window saw an obstacle.
  bool update(int rawLevel);

 private:
  int window_[cfg::IR_WINDOW];
  int index_ = 0;
};

// Tilt from an accelerometer: angle between the scooter's vertical axis and
// gravity. Must stay above the threshold for 500 ms before it counts (bumps),
// clears immediately when the scooter is upright again.
float tiltAngleDeg(float ax, float ay, float az);

class TiltDebounce {
 public:
  bool update(float angleDeg, uint32_t nowMs);

 private:
  bool timing_ = false;
  bool active_ = false;
  uint32_t sinceMs_ = 0;
};

// Throttle potentiometer: 12-bit ADC count -> 0-100 %, with a dead zone at the
// bottom so the motor does not creep when the throttle is "almost" released.
uint8_t throttlePercentFromAdc(int raw);

}  // namespace scooter
