// Types.h - the shared ride context.
//
// One Context object holds everything the control loop knows. Sensors write
// the flags, command links post intents, the safety controller decides what is
// allowed, the state machine moves between states, the ramp smooths the motor.
// Nothing in scooter_core touches hardware, so all of it runs in host tests.
#pragma once
#include <stdint.h>

namespace scooter {

enum class State : uint8_t { Locked, Unlocking, Riding, Warning, EmergencyStop };
enum class Zone : uint8_t { Clear, Slow, Red };
enum class Cause : uint8_t { None, Obstacle, Tilt, Theft, Geofence };

struct Context {
  // State: `requested` is a proposal, only StateMachine::step() changes `state`.
  State state = State::Locked;
  State requested = State::Locked;

  // Written by the sensor layer every tick.
  bool tilted = false;
  bool obstacle = false;
  uint8_t requestedThrottle = 0;  // rider's throttle, 0-100 %
  uint8_t batteryPercent = 100;

  // Emergency bookkeeping.
  bool emergencyLatched = false;  // hard emergency: needs an explicit RESET
  Cause cause = Cause::None;

  // Throttle pipeline: requested -> allowed (rules) -> motor (ramped).
  uint8_t allowedThrottle = 0;
  uint8_t motorThrottle = 0;

  // Geofence zone, pushed by the phone app (BLE) or the cloud (MQTT).
  Zone zone = Zone::Clear;
  bool redZoneActive = false;
  uint8_t redZoneStartThrottle = 0;
  uint32_t redZoneStartMs = 0;

  // Intents from the command links, consumed by the safety controller.
  bool lockRequested = false;
  bool unlockRequested = false;
  bool resetRequested = false;
  bool zoneUpdateRequested = false;
  Zone requestedZone = Zone::Clear;
};

const char* stateName(State s);   // "LOCKED", "RIDING", ...
const char* zoneName(Zone z);     // "CLEAR", "SLOW", "RED"
const char* causeName(Cause c);   // "NONE", "TILT", ...

}  // namespace scooter
