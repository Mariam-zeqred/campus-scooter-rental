#include "StateMachine.h"

#include "Config.h"

namespace scooter {

bool transitionAllowed(State from, State to, bool emergencyLatched) {
  switch (from) {
    case State::Locked:
      return to == State::Unlocking;
    case State::Unlocking:
      return to == State::Riding || to == State::Locked;
    case State::Riding:
      return to == State::Warning || to == State::EmergencyStop || to == State::Locked;
    case State::Warning:
      return to == State::Riding || to == State::EmergencyStop;
    case State::EmergencyStop:
      // A latched emergency holds the machine here until the latch is cleared.
      return !emergencyLatched && (to == State::Locked || to == State::Riding);
  }
  return false;
}

bool step(Context& ctx) {
  const State before = ctx.state;
  if (ctx.requested != ctx.state && transitionAllowed(ctx.state, ctx.requested, ctx.emergencyLatched)) {
    ctx.state = ctx.requested;
  }
  ctx.requested = ctx.state;  // a refused request is forgotten, not queued
  return ctx.state != before;
}

void rampThrottle(Context& ctx) {
  const uint8_t target = ctx.allowedThrottle;
  const uint8_t current = ctx.motorThrottle;
  if (current == target) return;

  uint8_t up = 0;
  uint8_t down = 0;
  switch (ctx.state) {
    case State::Locked:
    case State::Unlocking:
      up = cfg::RAMP_UP_UNLOCKING;
      down = cfg::RAMP_DOWN_RIDING;
      break;
    case State::Riding:
      up = cfg::RAMP_UP_RIDING;
      down = cfg::RAMP_DOWN_RIDING;
      break;
    case State::Warning:
      up = cfg::RAMP_UP_WARNING;
      down = cfg::RAMP_DOWN_WARNING;
      break;
    case State::EmergencyStop:
      up = 0;
      down = cfg::RAMP_DOWN_EMERGENCY;
      break;
  }

  if (current < target) {
    const uint8_t gap = target - current;
    ctx.motorThrottle = current + (up < gap ? up : gap);
  } else {
    const uint8_t gap = current - target;
    ctx.motorThrottle = current - (down < gap ? down : gap);
  }
}

}  // namespace scooter
