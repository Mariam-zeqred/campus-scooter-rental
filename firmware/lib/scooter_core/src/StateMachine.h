// StateMachine.h - the five-state ride machine and the throttle ramp.
//
//   LOCKED -> UNLOCKING -> RIDING <-> WARNING
//                 |          |  \        |
//                 v          v   v       v
//               LOCKED    LOCKED  EMERGENCY_STOP -> LOCKED (after RESET)
//                                        \-> RIDING (soft stop cleared)
//
// step() applies `ctx.requested` only if the transition is in the table, so a
// rule elsewhere can never jump the machine to a state it may not reach.
#pragma once
#include "Types.h"

namespace scooter {

// True if the machine may move from `from` to `to`.
bool transitionAllowed(State from, State to, bool emergencyLatched);

// Apply the requested transition (or ignore it). Returns true if state changed.
bool step(Context& ctx);

// Move motorThrottle one tick towards allowedThrottle, at a rate that depends
// on the state: gentle acceleration, firmer braking, near-instant in an emergency.
void rampThrottle(Context& ctx);

// Lock actuator and motor driver follow the state, nothing else.
inline bool lockEngaged(State s) { return s == State::Locked; }
inline bool motorEnabled(State s) { return s == State::Riding || s == State::Warning; }

}  // namespace scooter
