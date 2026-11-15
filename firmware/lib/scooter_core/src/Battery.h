// Battery.h - battery voltage through a resistor divider. [new in this rewrite]
//
// The prototype sketch reported a fixed 100 %. This module is what a real
// scooter needs: ADC count -> pack voltage -> a smoothed percentage. The
// percentage is a straight line between two voltages, which is only a rough
// stand-in for a real Li-ion discharge curve (see TODO.md).
#pragma once
#include <stdint.h>

namespace scooter {

float batteryVoltsFromAdc(int raw);
uint8_t batteryPercentFromVolts(float volts);

// Exponential moving average so one noisy ADC sample does not move the gauge.
class BatteryGauge {
 public:
  void update(int rawAdc);
  float volts() const { return volts_; }
  uint8_t percent() const { return batteryPercentFromVolts(volts_); }

 private:
  bool primed_ = false;
  float volts_ = 0.0f;
};

}  // namespace scooter
