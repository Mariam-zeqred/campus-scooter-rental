#include "Filters.h"

#include <math.h>

namespace scooter {

IrFilter::IrFilter() {
  for (int& v : window_) v = 1;  // start as "nothing in front"
}

bool IrFilter::update(int rawLevel) {
  window_[index_] = rawLevel ? 1 : 0;
  index_ = (index_ + 1) % cfg::IR_WINDOW;
  int sum = 0;
  for (int v : window_) sum += v;
  return 2 * sum < cfg::IR_WINDOW;  // average below 0.5
}

float tiltAngleDeg(float ax, float ay, float az) {
  return atan2f(sqrtf(ax * ax + ay * ay), az) * 180.0f / 3.14159265f;
}

bool TiltDebounce::update(float angleDeg, uint32_t nowMs) {
  if (angleDeg <= cfg::TILT_THRESHOLD_DEG) {
    timing_ = false;
    active_ = false;
    return false;
  }
  if (!timing_) {
    timing_ = true;
    sinceMs_ = nowMs;
  }
  if (nowMs - sinceMs_ >= cfg::TILT_DEBOUNCE_MS) active_ = true;
  return active_;
}

uint8_t throttlePercentFromAdc(int raw) {
  if (raw < cfg::THROTTLE_DEADZONE_ADC) return 0;
  if (raw > cfg::ADC_MAX) raw = cfg::ADC_MAX;
  const long span = cfg::ADC_MAX - cfg::THROTTLE_DEADZONE_ADC;
  return static_cast<uint8_t>((raw - cfg::THROTTLE_DEADZONE_ADC) * 100L / span);
}

}  // namespace scooter
