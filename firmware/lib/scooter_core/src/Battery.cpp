#include "Battery.h"

#include "Config.h"

namespace scooter {

float batteryVoltsFromAdc(int raw) {
  if (raw < 0) raw = 0;
  if (raw > cfg::ADC_MAX) raw = cfg::ADC_MAX;
  const float pinVolts = static_cast<float>(raw) / cfg::ADC_MAX * cfg::ADC_REF_VOLTS;
  return pinVolts * cfg::BATTERY_DIVIDER_RATIO;
}

uint8_t batteryPercentFromVolts(float volts) {
  if (volts <= cfg::BATTERY_EMPTY_VOLTS) return 0;
  if (volts >= cfg::BATTERY_FULL_VOLTS) return 100;
  const float span = cfg::BATTERY_FULL_VOLTS - cfg::BATTERY_EMPTY_VOLTS;
  return static_cast<uint8_t>((volts - cfg::BATTERY_EMPTY_VOLTS) * 100.0f / span + 0.5f);
}

void BatteryGauge::update(int rawAdc) {
  const float v = batteryVoltsFromAdc(rawAdc);
  if (!primed_) {
    volts_ = v;
    primed_ = true;
  } else {
    volts_ += 0.05f * (v - volts_);
  }
}

}  // namespace scooter
