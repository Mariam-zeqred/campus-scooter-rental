# Walkthrough (for Mariam)

This is your graduation project, rewritten in 2026 from your January 2026 sketch and the two project books. The one-sentence version for an interview: *"The original repository was lost; I rebuilt my parts, the firmware and the cloud, from my surviving sketch and the project books, and I say in the README what was never run."*

## Why it is designed this way

- **The logic has no hardware in it.** `scooter_core` never calls a pin, so the state machine and safety rules run in tests on a laptop. That is the proof a reader can check without a board.
- **Links ask, the safety controller decides.** BLE and MQTT only post requests. `Safety.cpp` may refuse them, and `StateMachine.cpp` only allows transitions that are in its table.
- **Slow things live elsewhere.** Wi-Fi and TLS run in their own task on core 0, so the 50 Hz loop never waits for the network.
- **In the cloud, each function gets only the permissions it uses**, and the ID photo is treated as personal data (private bucket, deleted after 30 days).

## File map

| File | What to know |
|---|---|
| `firmware/original/scooter_2026-01-25.ino` | Your sketch, untouched. |
| `firmware/lib/scooter_core/src/Safety.cpp` | The rules. Start here; it is your `SafetyController`. |
| `.../StateMachine.cpp` | Transition table and throttle ramp. |
| `firmware/src/main.cpp` | The loop, eight numbered steps. |
| `firmware/src/CloudLink.cpp` | Wi-Fi/MQTT task. New, never run. |
| `firmware/include/Config.h` | Every number, tagged `[sketch]`, `[book]` or `[new]`. |
| `cloud/stacks/scooter_stack.py` | All AWS resources in one file. |
| `cloud/lambdas/*/handler.py` | ID check, telemetry, ride API. |
| `docs/FIRMWARE.md` | What changed from your sketch. Read before any interview. |

## Three questions a professor may ask

**1. "Which parts are yours, and did this run on a scooter?"**
Mine: the firmware, the hardware and the AWS side; I led the team. The app, its design and the Node API were teammates' work (`docs/TEAM.md`). The prototype was a desk-sized model: ESP32, IR sensor, tilt sensor, potentiometer as throttle, LCD, buzzer, small motors. My sketch ran on it. This rewrite compiles and passes its tests but has not been flashed, and GPS, battery sensing, the lock servo and MQTT are additions.

**2. "How do you know the safety logic is correct?"**
26 host tests drive the same three functions the loop calls. Porting the sketch under tests exposed four faults in it. The clearest: a `RESET` sent while nothing was wrong stayed pending and would silently cancel the next tilt emergency. Three gaps remain and are listed, the main one being that `UNLOCK` over BLE is not authenticated.

**3. "Why both BLE and MQTT, and what does the cloud cost?"**
BLE because unlocking must work instantly and without internet; MQTT because the operator needs every scooter's state, and one open session is cheaper than an HTTPS request per message. About $0.50 per scooter per month under the stated assumptions, nearly all of it telemetry messages; reporting less often while parked is the main lever.

## One thing you could extend

Authenticate the unlock. When a ride is booked, the cloud creates a short-lived token signed with a key shared with that scooter (an HMAC over scooter id, trip id and expiry). The phone passes it over BLE; the scooter checks the signature and expiry before accepting `UNLOCK`. It needs one new function in `scooter_core` with host tests and a few lines in the ride API.

## Team lists

The first book lists eight students and the final book four, because the team was eight in the first semester and four in the second. `docs/TEAM.md` says so and copies both title pages as printed.
