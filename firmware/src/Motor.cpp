#include "Motor.h"

#include <Arduino.h>

#include "Config.h"
#include "StateMachine.h"

namespace motor {

namespace {

constexpr uint8_t kMotorChannel = 0;
constexpr uint8_t kServoChannel = 2;  // a different LEDC timer than the motor
constexpr uint32_t kServoFreq = 50;
constexpr uint8_t kServoBits = 16;

// The LEDC (PWM) API changed between Arduino-ESP32 2.x and 3.x.
void pwmAttach(int pin, uint8_t channel, uint32_t freq, uint8_t bits) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcAttach(pin, freq, bits);
#else
  ledcSetup(channel, freq, bits);
  ledcAttachPin(pin, channel);
#endif
}

void pwmWrite(int pin, uint8_t channel, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

// Hobby servo: 0.5 ms pulse = 0 degrees, 2.5 ms = 180 degrees, every 20 ms.
uint32_t servoDuty(int degrees) {
  const uint32_t pulseUs = 500 + static_cast<uint32_t>(degrees) * 2000 / 180;
  return pulseUs * ((1u << kServoBits) - 1) / 20000;
}

bool lastLocked = true;

}  // namespace

void begin() {
  pwmAttach(cfg::PIN_MOTOR_PWM, kMotorChannel, cfg::MOTOR_PWM_FREQ, cfg::MOTOR_PWM_BITS);
  pwmWrite(cfg::PIN_MOTOR_PWM, kMotorChannel, 0);  // motor off at boot
  if (cfg::LOCK_SERVO_FITTED) {
    pwmAttach(cfg::PIN_LOCK_SERVO, kServoChannel, kServoFreq, kServoBits);
    pwmWrite(cfg::PIN_LOCK_SERVO, kServoChannel, servoDuty(cfg::LOCK_SERVO_LOCKED_DEG));
  }
}

void apply(const scooter::Context& ctx) {
  const uint32_t maxDuty = (1u << cfg::MOTOR_PWM_BITS) - 1;
  const uint32_t duty = scooter::motorEnabled(ctx.state) ? ctx.motorThrottle * maxDuty / 100 : 0;
  pwmWrite(cfg::PIN_MOTOR_PWM, kMotorChannel, duty);

  const bool locked = scooter::lockEngaged(ctx.state);
  if (cfg::LOCK_SERVO_FITTED && locked != lastLocked) {
    pwmWrite(cfg::PIN_LOCK_SERVO, kServoChannel,
             servoDuty(locked ? cfg::LOCK_SERVO_LOCKED_DEG : cfg::LOCK_SERVO_OPEN_DEG));
  }
  lastLocked = locked;
}

}  // namespace motor
