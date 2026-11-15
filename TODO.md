# TODO

What is left, in the order that would add the most. Items 1 to 3 need things that were not available when this rewrite was made (a board, the author's confirmation, an AWS account).

## Needs hardware

1. **Flash an ESP32 and walk the state machine by hand.** Same parts as the sketch (ADXL345, IR module, potentiometer, 16x2 I2C LCD, buzzer). Check each transition in `docs/FIRMWARE.md`, the buzzer patterns, and that the watchdog does not fire during BLE and Wi-Fi start-up. Record what was observed in the README.
2. **Measure the loop.** Worst-case loop time with BLE notifying and MQTT connecting; free heap after the TLS handshake. The image is 94 % of its flash slot: try NimBLE instead of the default Bluetooth stack if it has to shrink.
3. **Connect the optional parts one at a time** (battery divider, NEO-6M, lock servo), turning on their switch in `Config.h`, and replace the placeholder constants (divider ratio, pack voltages, servo angles) with measured ones.

## Needs the author

4. Confirm the names in `docs/TEAM.md` and whether the January 2026 sketch is the revision that was presented.
5. Add photographs of the built prototype to `docs/HARDWARE.md`.

## Firmware

6. **Authenticate `UNLOCK`.** A short-lived ride token issued at booking, signed with a per-scooter key (HMAC over scooter id, trip id, expiry), checked on the scooter before the command is accepted. Core function plus host tests; the BLE characteristic then carries `UNLOCK <token>`.
7. **Retry the lock after a BLE disconnect** until it is granted, instead of asking once.
8. **Decide the soft-stop resume rule**: after an obstacle clears, require the throttle to return to zero before riding again.
9. Battery percentage from a discharge curve instead of a straight line; low-battery `WARNING` during a ride (the book mentions it, the sketch does not have it).
10. Keep the offline outbox in flash (the book's design) so a reboot does not lose it.
11. Use the theft and geofence causes that exist in the enum but are never raised.

## Cloud

12. **Package the barcode decoder** (Pillow + pyzbar + zbar) as a Lambda layer or container image, and test it on a synthetic card image generated in the test.
13. **Replace the scooter scan** with a geohash or status index so "scooters near me" does not read the whole table.
14. Deploy once to a personal account, record real Lambda durations, and replace the assumed run times in `cost.py`.
15. A scheduled check for scooters that have not reported (stale `lastSeen`) and for bookings that were never ended.
16. Payments and reviews: the tables or collections are in the book; no code exists here.

## Housekeeping

17. `pio test -e native` runs in CI but not on the Windows PC used for the rewrite (no host GCC there); `tools/check.py` covers the same tests with another compiler.
18. The CI build uses the official PlatformIO ESP32 platform (Arduino core 2.x); the local build used Arduino core 3.3.11. Both compile; pin one once a board has been flashed.
