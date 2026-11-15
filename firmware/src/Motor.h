// Motor.h - the two actuators: motor PWM and the lock servo.
// The motor PWM is from the sketch. The lock servo is new in this rewrite and
// only driven when cfg::LOCK_SERVO_FITTED is true.
#pragma once
#include "Types.h"

namespace motor {

void begin();
// Called every tick: PWM follows motorThrottle (zero unless in a ride),
// the lock is engaged only in LOCKED.
void apply(const scooter::Context& ctx);

}  // namespace motor
