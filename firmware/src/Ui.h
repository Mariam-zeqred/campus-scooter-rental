// Ui.h - rider feedback: 16x2 LCD and buzzer.
// Ported from the UIManager class of the sketch. The sketch beeped with
// delay() calls, which froze the 50 Hz loop for 300 ms; here the beeps are
// timed with millis() so the safety loop never waits for the buzzer.
#pragma once
#include <LiquidCrystal_I2C.h>

#include "Types.h"

class Ui {
 public:
  void begin();
  void update(const scooter::Context& ctx, uint32_t nowMs);

 private:
  void drawScreen(const scooter::Context& ctx, bool resetBlocked);
  void updateBuzzer(const scooter::Context& ctx, uint32_t nowMs);

  LiquidCrystal_I2C lcd_{0x27, 16, 2};
  int shownScreen_ = -1;     // which screen is on the display now
  int shownBattery_ = -1;
  int shownThrottle_ = -1;

  scooter::State lastState_ = scooter::State::Locked;
  bool buzzerOn_ = false;
  uint32_t buzzerToggledMs_ = 0;
  int beepsLeft_ = 0;        // short confirmation beeps still to play
};
