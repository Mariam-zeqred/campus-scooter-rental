// Sensors.h - reads the real pins and fills the Context flags.
// Ported from the SensorsManager class of the sketch; the filtering itself
// lives in scooter_core (Filters, Battery) so it can be tested on the host.
#pragma once
#include <Adafruit_ADXL345_U.h>

#include "Battery.h"
#include "Filters.h"
#include "Types.h"

class Sensors {
 public:
  void begin();
  void update(scooter::Context& ctx, uint32_t nowMs);
  float batteryVolts() const { return battery_.volts(); }

 private:
  Adafruit_ADXL345_Unified accel_{12345};
  bool accelOk_ = false;
  uint32_t lastAccelMs_ = 0;
  scooter::IrFilter ir_;
  scooter::TiltDebounce tilt_;
  scooter::BatteryGauge battery_;
};
