#include "Telemetry.h"

#include <stdio.h>

namespace scooter {

namespace {

// snprintf reports the length it wanted; treat "did not fit" as failure.
size_t fitted(int written, size_t size) {
  return (written > 0 && static_cast<size_t>(written) < size) ? static_cast<size_t>(written) : 0;
}

}  // namespace

size_t buildBleStatus(const Context& ctx, char* buf, size_t size) {
  return fitted(snprintf(buf, size, "{\"state\":\"%s\",\"zone\":\"%s\",\"throttle\":%u,\"battery\":%u}",
                         stateName(ctx.state), zoneName(ctx.zone), ctx.motorThrottle, ctx.batteryPercent),
                size);
}

size_t buildCloudRecord(const CloudSample& s, char* buf, size_t size) {
  char gps[64];
  if (s.gps.valid) {
    snprintf(gps, sizeof(gps), "{\"lat\":%.6f,\"lng\":%.6f}", s.gps.lat, s.gps.lng);
  } else {
    snprintf(gps, sizeof(gps), "null");  // no fix yet: say so instead of sending 0,0
  }
  return fitted(snprintf(buf, size,
                         "{\"device_id\":\"%s\",\"timestamp\":%lu,\"state\":\"%s\",\"zone\":\"%s\","
                         "\"throttle\":%u,\"gps\":%s,\"battery\":{\"voltage\":%.1f,\"percent\":%u},"
                         "\"diagnostics\":{\"rssi\":%d,\"uptime\":%lu}}",
                         s.deviceId, static_cast<unsigned long>(s.timestamp), stateName(s.state),
                         zoneName(s.zone), s.throttle, gps, static_cast<double>(s.batteryVolts),
                         s.batteryPercent, s.rssi, static_cast<unsigned long>(s.uptimeS)),
                size);
}

size_t buildAlert(const char* deviceId, uint32_t timestamp, Cause cause, char* buf, size_t size) {
  return fitted(snprintf(buf, size, "{\"device_id\":\"%s\",\"timestamp\":%lu,\"alert\":\"%s\"}", deviceId,
                         static_cast<unsigned long>(timestamp), causeName(cause)),
                size);
}

size_t telemetryTopic(const char* org, const char* device, char* buf, size_t size) {
  return fitted(snprintf(buf, size, "campus/%s/scooter/%s/telemetry", org, device), size);
}

size_t commandTopic(const char* org, const char* device, char* buf, size_t size) {
  return fitted(snprintf(buf, size, "campus/%s/scooter/%s/cmd", org, device), size);
}

size_t alertTopic(const char* org, char* buf, size_t size) {
  return fitted(snprintf(buf, size, "campus/%s/alerts/critical", org), size);
}

}  // namespace scooter
