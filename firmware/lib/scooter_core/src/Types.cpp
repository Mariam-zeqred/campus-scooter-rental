#include "Types.h"

namespace scooter {

const char* stateName(State s) {
  switch (s) {
    case State::Locked: return "LOCKED";
    case State::Unlocking: return "UNLOCKING";
    case State::Riding: return "RIDING";
    case State::Warning: return "WARNING";
    case State::EmergencyStop: return "EMERGENCY";  // spelling the sketch sent to the app
  }
  return "?";
}

const char* zoneName(Zone z) {
  switch (z) {
    case Zone::Clear: return "CLEAR";
    case Zone::Slow: return "SLOW";
    case Zone::Red: return "RED";
  }
  return "?";
}

const char* causeName(Cause c) {
  switch (c) {
    case Cause::None: return "NONE";
    case Cause::Obstacle: return "OBSTACLE";
    case Cause::Tilt: return "TILT";
    case Cause::Theft: return "THEFT";
    case Cause::Geofence: return "GEOFENCE";
  }
  return "?";
}

}  // namespace scooter
