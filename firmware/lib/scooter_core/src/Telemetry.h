// Telemetry.h - the messages the scooter sends, and the topics they go to.
//
// Two formats:
//  * the short BLE status the phone app reads 5 times a second (same keys as
//    the prototype sketch);
//  * the cloud record published over MQTT, following the payload in the
//    project book (ch. 9.4.1). [new in this rewrite]
// Topic names follow the book's tree: campus/{org}/scooter/{device}/...
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "Nmea.h"
#include "Types.h"

namespace scooter {

// Everything the cloud record needs, copied out of the control loop.
struct CloudSample {
  const char* deviceId = "";
  uint32_t timestamp = 0;  // Unix seconds, 0 if the clock is not set yet
  State state = State::Locked;
  Zone zone = Zone::Clear;
  uint8_t throttle = 0;
  GpsFix gps;
  float batteryVolts = 0.0f;
  uint8_t batteryPercent = 0;
  int rssi = 0;            // Wi-Fi signal, dBm
  uint32_t uptimeS = 0;
};

// Each builder writes a NUL-terminated JSON string and returns its length,
// or 0 if the buffer is too small.
size_t buildBleStatus(const Context& ctx, char* buf, size_t size);
size_t buildCloudRecord(const CloudSample& s, char* buf, size_t size);
size_t buildAlert(const char* deviceId, uint32_t timestamp, Cause cause, char* buf, size_t size);

size_t telemetryTopic(const char* org, const char* device, char* buf, size_t size);
size_t commandTopic(const char* org, const char* device, char* buf, size_t size);
size_t alertTopic(const char* org, char* buf, size_t size);

}  // namespace scooter
