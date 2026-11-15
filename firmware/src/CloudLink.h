// CloudLink.h - Wi-Fi, MQTT over TLS (AWS IoT Core) and the GPS serial port.
// [new in this rewrite; the sketch had no Wi-Fi code. Compiles, never run on hardware.]
//
// Everything here runs in its own FreeRTOS task pinned to core 0. A TLS
// handshake can block for seconds; the 50 Hz safety loop on core 1 must never
// wait for it. The two sides talk through queues only:
//   control loop -> publishSnapshot() / raiseAlert()
//   cloud task   -> IntentBox (commands received over MQTT)
#pragma once
#include "Commands.h"
#include "Types.h"

namespace cloud {

struct Snapshot {
  scooter::State state;
  scooter::Zone zone;
  uint8_t throttle;
  uint8_t batteryPercent;
  float batteryVolts;
};

void begin(scooter::IntentBox& intents);
void publishSnapshot(const Snapshot& s);  // keeps only the latest
void raiseAlert(scooter::Cause cause);    // queued, sent with QoS 1

}  // namespace cloud
