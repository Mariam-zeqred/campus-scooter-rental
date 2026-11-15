#include "Ui.h"

#include <Arduino.h>

#include "Config.h"

using scooter::Cause;
using scooter::State;
using scooter::Zone;

void Ui::begin() {
  lcd_.init();
  lcd_.backlight();
  lcd_.setCursor(0, 0);
  lcd_.print("Smart Scooter");
  lcd_.setCursor(0, 1);
  lcd_.print("Booting...");
  pinMode(cfg::PIN_BUZZER, OUTPUT);
  digitalWrite(cfg::PIN_BUZZER, LOW);
}

void Ui::update(const scooter::Context& ctx, uint32_t nowMs) {
  // A latched emergency with a RESET waiting means "fix the conditions first".
  const bool resetBlocked = ctx.state == State::EmergencyStop && ctx.emergencyLatched && ctx.resetRequested;
  const int screen = static_cast<int>(ctx.state) * 2 + (resetBlocked ? 1 : 0);
  if (screen != shownScreen_) {
    lcd_.clear();
    drawScreen(ctx, resetBlocked);
    shownScreen_ = screen;
    shownBattery_ = -1;
    shownThrottle_ = -1;
  }
  if (ctx.batteryPercent != shownBattery_) {
    lcd_.setCursor(11, 0);
    lcd_.print("    ");
    lcd_.setCursor(11, 0);
    lcd_.print(ctx.batteryPercent);
    lcd_.print("%");
    shownBattery_ = ctx.batteryPercent;
  }
  if (ctx.state == State::Riding && ctx.motorThrottle != shownThrottle_) {
    lcd_.setCursor(5, 1);
    lcd_.print("    ");
    lcd_.setCursor(5, 1);
    lcd_.print(ctx.motorThrottle);
    lcd_.print("%");
    shownThrottle_ = ctx.motorThrottle;
  }
  updateBuzzer(ctx, nowMs);
}

void Ui::drawScreen(const scooter::Context& ctx, bool resetBlocked) {
  const char* top = "";
  const char* bottom = "";
  switch (ctx.state) {
    case State::Locked:
      top = "LOCKED";
      bottom = "Scan to Unlock";
      break;
    case State::Unlocking:
      top = "UNLOCKING";
      bottom = "Safety Check";
      break;
    case State::Riding:
      top = "RIDING";
      bottom = "THR: ";
      break;
    case State::Warning:
      top = "WARNING";
      bottom = ctx.zone == Zone::Red ? "Restricted Area" : "Proceed w/ Care";
      break;
    case State::EmergencyStop:
      if (resetBlocked) {
        top = "RESET BLOCKED";
        bottom = "Fix Conditions";
      } else {
        top = "EMERGENCY";
        bottom = ctx.cause == Cause::Tilt ? "Scooter Tilted"
                 : ctx.cause == Cause::Obstacle ? "Obstacle Block" : "System Halted";
      }
      break;
  }
  lcd_.setCursor(0, 0);
  lcd_.print(top);
  lcd_.setCursor(0, 1);
  lcd_.print(bottom);
}

void Ui::updateBuzzer(const scooter::Context& ctx, uint32_t nowMs) {
  if (ctx.state != lastState_) {
    // Entering a soft (obstacle) stop: two short beeps, then quiet.
    beepsLeft_ = (ctx.state == State::EmergencyStop && !ctx.emergencyLatched) ? 2 : 0;
    buzzerOn_ = false;
    buzzerToggledMs_ = nowMs;
    lastState_ = ctx.state;
  }

  bool want = false;
  if (ctx.state == State::EmergencyStop && ctx.emergencyLatched) {
    want = true;  // continuous tone until reset
  } else if (ctx.state == State::Warning) {
    if (nowMs - buzzerToggledMs_ >= 500) {  // slow chirp
      buzzerOn_ = !buzzerOn_;
      buzzerToggledMs_ = nowMs;
    }
    want = buzzerOn_;
  } else if (beepsLeft_ > 0) {
    if (nowMs - buzzerToggledMs_ >= 100) {
      if (buzzerOn_) --beepsLeft_;
      buzzerOn_ = !buzzerOn_ && beepsLeft_ > 0;
      buzzerToggledMs_ = nowMs;
    }
    want = buzzerOn_;
  }
  digitalWrite(cfg::PIN_BUZZER, want ? HIGH : LOW);
}
