// Campus scooter controller - ESP32.
//
// The control loop runs 50 times a second on core 1, in a fixed order taken
// from the prototype sketch:
//   1 commands  2 sensors  3 safety rules  4 state machine  5 throttle ramp
//   6 motor + lock  7 display + buzzer  8 status out
// Nothing in the loop blocks. Wi-Fi and MQTT live in their own task on core 0
// (CloudLink.cpp); BLE callbacks run on the Bluetooth task. Both only post
// commands into `intents`, which the loop drains in step 1.
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "BleLink.h"
#include "CloudLink.h"
#include "Commands.h"
#include "Config.h"
#include "Motor.h"
#include "Safety.h"
#include "Sensors.h"
#include "StateMachine.h"
#include "Types.h"
#include "Ui.h"

static scooter::Context ctx;
static scooter::SafetyController safety;
static scooter::IntentBox intents;
static Sensors sensors;
static Ui ui;

static uint32_t lastTickMs = 0;
static uint32_t lastBleStatusMs = 0;

// If the loop ever hangs, the watchdog resets the chip; it boots LOCKED with
// the motor off. The sketch only had a comment where this belongs.
static void startWatchdog() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = cfg::WATCHDOG_TIMEOUT_MS;
  wdt.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(cfg::WATCHDOG_TIMEOUT_MS / 1000, true);
#endif
  esp_task_wdt_add(nullptr);
}

void setup() {
  Serial.begin(115200);
  motor::begin();  // first: motor output low before anything else can fail
  sensors.begin();
  ui.begin();
  ble::begin(intents);
  if (cfg::CLOUD_LINK_ENABLED) cloud::begin(intents);
  startWatchdog();
  Serial.println("[boot] scooter controller ready, state LOCKED");
}

void loop() {
  const uint32_t now = millis();
  if (now - lastTickMs < cfg::TICK_MS) {
    delay(1);
    return;
  }
  lastTickMs = now;

  intents.drainInto(ctx);                       // 1 commands from BLE / MQTT
  sensors.update(ctx, now);                     // 2 obstacle, throttle, tilt, battery
  const bool wasLatched = ctx.emergencyLatched;
  safety.evaluate(ctx, now);                    // 3 what is allowed right now
  if (scooter::step(ctx)) {                     // 4 state machine
    Serial.printf("[state] %s\n", scooter::stateName(ctx.state));
  }
  scooter::rampThrottle(ctx);                   // 5 smooth the motor command
  motor::apply(ctx);                            // 6 PWM and lock
  ui.update(ctx, now);                          // 7 LCD and buzzer

  // 8 status out: alert on a new hard emergency, snapshot for the cloud task,
  // and the 5 Hz BLE status for the phone.
  if (cfg::CLOUD_LINK_ENABLED) {
    if (!wasLatched && ctx.emergencyLatched) cloud::raiseAlert(ctx.cause);
    cloud::publishSnapshot({ctx.state, ctx.zone, ctx.motorThrottle, ctx.batteryPercent, sensors.batteryVolts()});
  }
  if (ble::connected() && now - lastBleStatusMs >= cfg::BLE_TELEMETRY_MS) {
    lastBleStatusMs = now;
    ble::notifyStatus(ctx);
  }
  ble::poll(now);

  esp_task_wdt_reset();
}
