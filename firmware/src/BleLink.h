// BleLink.h - the phone link: a BLE GATT server with three characteristics.
// Same service and characteristic UUIDs as the sketch, so an app written for
// the prototype would still find them.
//   command   (write)        LOCK / UNLOCK / RESET
//   zone      (write)        CLEAR / SLOW / RED   (geofence result from the phone)
//   status    (read, notify) {"state":..,"zone":..,"throttle":..,"battery":..}
#pragma once
#include "Commands.h"
#include "Types.h"

namespace ble {

void begin(scooter::IntentBox& intents);
bool connected();
void notifyStatus(const scooter::Context& ctx);
// Call from loop(): restarts advertising a moment after a phone disconnects.
void poll(uint32_t nowMs);

}  // namespace ble
