#include "Sensors.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"

void Sensors::begin() {
  Wire.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL);
  accelOk_ = accel_.begin();
  if (accelOk_) {
    accel_.setRange(ADXL345_RANGE_8_G);
  } else {
    // As in the sketch: carry on without tilt detection rather than refuse to boot.
    Serial.println("[sensors] ADXL345 not found, tilt detection off");
  }
  pinMode(cfg::PIN_IR, INPUT);
  pinMode(cfg::PIN_THROTTLE, INPUT);
  if (cfg::BATTERY_SENSE_FITTED) pinMode(cfg::PIN_BATTERY, INPUT);
}

void Sensors::update(scooter::Context& ctx, uint32_t nowMs) {
  // Every tick (50 Hz): obstacle and throttle.
  ctx.obstacle = ir_.update(digitalRead(cfg::PIN_IR));
  ctx.requestedThrottle = scooter::throttlePercentFromAdc(analogRead(cfg::PIN_THROTTLE));

  if (cfg::BATTERY_SENSE_FITTED) {
    battery_.update(analogRead(cfg::PIN_BATTERY));
    ctx.batteryPercent = battery_.percent();
  } else {
    ctx.batteryPercent = 100;  // the prototype ran from USB; same fixed value as the sketch
  }

  // Every 50 ms: tilt from the accelerometer.
  if (accelOk_ && nowMs - lastAccelMs_ >= cfg::ACCEL_INTERVAL_MS) {
    lastAccelMs_ = nowMs;
    sensors_event_t e;
    accel_.getEvent(&e);
    const float angle = scooter::tiltAngleDeg(e.acceleration.x, e.acceleration.y, e.acceleration.z);
    ctx.tilted = tilt_.update(angle, nowMs);
  }
}
