#include "Safety.h"

#include "Config.h"

namespace scooter {

bool SafetyController::canRecover(const Context& ctx) {
  return !ctx.tilted && !ctx.obstacle && ctx.requestedThrottle == 0 && ctx.zone != Zone::Red;
}

void SafetyController::evaluate(Context& ctx, uint32_t nowMs) {
  // A latched (hard) emergency overrides everything else.
  if (ctx.emergencyLatched) {
    ctx.allowedThrottle = 0;
    ctx.requested = State::EmergencyStop;
    if (ctx.resetRequested && canRecover(ctx)) {
      ctx.emergencyLatched = false;
      ctx.cause = Cause::None;
      ctx.resetRequested = false;
      ctx.requested = State::Locked;  // always re-enter through LOCKED
    }
    // A RESET that cannot be honoured yet stays pending, as in the sketch:
    // the display shows "RESET BLOCKED" until the conditions are fixed.
    return;
  }
  // Changed from the sketch: a RESET sent when nothing is latched is dropped.
  // The sketch kept the flag, so a later emergency would have cleared itself.
  ctx.resetRequested = false;

  handleTilt(ctx, nowMs);
  handleObstacle(ctx);
  handleLockUnlock(ctx);
  handleZoneUpdate(ctx);
  applyZoneRules(ctx, nowMs);

  // UNLOCKING is the pre-ride safety check; it completes on its own.
  if (ctx.state == State::Unlocking) {
    const bool safeToRide = !ctx.tilted && !ctx.obstacle && ctx.zone != Zone::Red &&
                            ctx.batteryPercent > cfg::MIN_BATTERY_PERCENT;  // battery check: from the book
    if (safeToRide) ctx.requested = State::Riding;
  }
}

void SafetyController::handleTilt(Context& ctx, uint32_t nowMs) {
  // A parked scooter lying on its side is not an emergency.
  if (ctx.state != State::Riding && ctx.state != State::Warning) {
    tiltTiming_ = false;
    return;
  }
  if (!ctx.tilted) {
    tiltTiming_ = false;
    return;
  }
  if (!tiltTiming_) {
    tiltTiming_ = true;
    tiltStartMs_ = nowMs;
  }
  if (nowMs - tiltStartMs_ >= cfg::TILT_EMERGENCY_MS) {
    ctx.emergencyLatched = true;
    ctx.cause = Cause::Tilt;
    ctx.requested = State::EmergencyStop;
  }
}

void SafetyController::handleObstacle(Context& ctx) {
  // Soft emergency: stop while something is in front, resume when it is gone.
  // Changed from the sketch: also honoured in WARNING (the sketch only looked
  // while RIDING, so an obstacle inside a red zone was ignored).
  const bool moving = ctx.state == State::Riding || ctx.state == State::Warning;
  if (moving && ctx.obstacle) {
    ctx.cause = Cause::Obstacle;
    ctx.requested = State::EmergencyStop;
  }
  // evaluate() returns early while latched, so an EMERGENCY_STOP seen here is
  // always the soft kind. Changed from the sketch: the exit no longer depends
  // on `cause`, which the sketch could clear while still stopped (a dead end).
  if (ctx.state == State::EmergencyStop && !ctx.obstacle) {
    ctx.cause = Cause::None;
    ctx.requested = State::Riding;
  }
}

void SafetyController::handleLockUnlock(Context& ctx) {
  if (ctx.unlockRequested) {
    if (ctx.state == State::Locked) ctx.requested = State::Unlocking;
    ctx.unlockRequested = false;
  }
  if (ctx.lockRequested) {
    const bool stopped = ctx.allowedThrottle == 0;  // rider has released the throttle
    const bool allowedHere = ctx.zone != Zone::Red;  // no parking in a red zone
    if (stopped && allowedHere && ctx.state != State::EmergencyStop) ctx.requested = State::Locked;
    ctx.lockRequested = false;  // a denied lock is dropped, not retried
  }
}

void SafetyController::handleZoneUpdate(Context& ctx) {
  if (!ctx.zoneUpdateRequested) return;
  ctx.zone = ctx.requestedZone;
  ctx.zoneUpdateRequested = false;
}

void SafetyController::applyZoneRules(Context& ctx, uint32_t nowMs) {
  // Leaving a red zone: forget the ramp and go back to normal riding.
  if (ctx.zone != Zone::Red && ctx.redZoneActive) {
    ctx.redZoneActive = false;
    if (ctx.state == State::Warning) ctx.requested = State::Riding;
  }

  switch (ctx.zone) {
    case Zone::Clear:
      ctx.allowedThrottle = ctx.requestedThrottle;
      break;
    case Zone::Slow:
      ctx.allowedThrottle = ctx.requestedThrottle < cfg::SLOW_ZONE_CAP_PERCENT
                                ? ctx.requestedThrottle
                                : cfg::SLOW_ZONE_CAP_PERCENT;
      break;
    case Zone::Red: {
      // Controlled stop: ramp from the speed at entry down to zero over 3 s.
      // Changed from the sketch: only a ride in progress is moved to WARNING,
      // and an emergency request made earlier in this tick is not overwritten.
      if (ctx.state == State::Riding && ctx.requested != State::EmergencyStop) {
        ctx.requested = State::Warning;
      }
      if (!ctx.redZoneActive) {
        ctx.redZoneActive = true;
        ctx.redZoneStartMs = nowMs;
        ctx.redZoneStartThrottle = ctx.motorThrottle;
      }
      const uint32_t elapsed = nowMs - ctx.redZoneStartMs;
      if (elapsed >= cfg::RED_ZONE_RAMP_MS) {
        ctx.allowedThrottle = 0;
      } else {
        const float ratio = 1.0f - static_cast<float>(elapsed) / cfg::RED_ZONE_RAMP_MS;
        ctx.allowedThrottle = static_cast<uint8_t>(ctx.redZoneStartThrottle * ratio);
      }
      break;
    }
  }

  // Final authority: no throttle unless the scooter is actually in a ride.
  if (ctx.state != State::Riding && ctx.state != State::Warning) ctx.allowedThrottle = 0;
}

}  // namespace scooter
