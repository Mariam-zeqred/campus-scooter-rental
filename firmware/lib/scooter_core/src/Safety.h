// Safety.h - the rules that decide what the rider is allowed to do.
//
// evaluate() runs once per 20 ms tick, after the sensors and before the state
// machine. It never drives hardware: it sets `allowedThrottle` and proposes a
// state in `ctx.requested`. Ported from the SafetyController class of the
// prototype sketch; the differences are listed in docs/FIRMWARE.md.
#pragma once
#include "Types.h"

namespace scooter {

class SafetyController {
 public:
  void evaluate(Context& ctx, uint32_t nowMs);

  // A latched emergency may only be reset when the scooter is upright, the
  // path is clear, the throttle is released and it is not inside a red zone.
  static bool canRecover(const Context& ctx);

 private:
  void handleTilt(Context& ctx, uint32_t nowMs);
  void handleObstacle(Context& ctx);
  void handleLockUnlock(Context& ctx);
  void handleZoneUpdate(Context& ctx);
  void applyZoneRules(Context& ctx, uint32_t nowMs);

  bool tiltTiming_ = false;
  uint32_t tiltStartMs_ = 0;
};

}  // namespace scooter
