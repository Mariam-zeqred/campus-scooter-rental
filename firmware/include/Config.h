// Config.h - every pin and tuning number in one place.
//
// Values marked [sketch] are copied from the January 2026 prototype sketch
// (firmware/original/). Values marked [book] come from the project book.
// Values marked [new] were chosen for this rewrite and never ran on hardware.
#pragma once
#include <stdint.h>

namespace cfg {

// ---- Pins (ESP32 dev board) ----------------------------------------------
constexpr int PIN_I2C_SDA = 21;      // [sketch] LCD backpack + ADXL345
constexpr int PIN_I2C_SCL = 22;      // [sketch]
constexpr int PIN_BUZZER = 25;       // [sketch]
constexpr int PIN_MOTOR_PWM = 26;    // [sketch] motor driver PWM input
constexpr int PIN_IR = 27;           // [sketch] IR obstacle module, active low
constexpr int PIN_THROTTLE = 34;     // [sketch] potentiometer, ADC1
constexpr int PIN_BATTERY = 35;      // [new] battery divider, ADC1 (ADC2 is unusable with Wi-Fi on)
constexpr int PIN_LOCK_SERVO = 13;   // [new] lock servo signal
constexpr int PIN_GPS_RX = 16;       // [new] UART2 RX <- NEO-6M TX
constexpr int PIN_GPS_TX = 17;       // [new] UART2 TX -> NEO-6M RX

// ---- What is fitted ---------------------------------------------------------
// The desk prototype had none of these three. With a flag off, the firmware
// behaves like the sketch (battery shown as 100 %, no position, no lock servo).
constexpr bool BATTERY_SENSE_FITTED = false;  // [new]
constexpr bool GPS_FITTED = false;            // [new]
constexpr bool LOCK_SERVO_FITTED = false;     // [new]
constexpr bool CLOUD_LINK_ENABLED = true;     // [new] Wi-Fi + MQTT task; BLE works without it

// ---- Control loop ----------------------------------------------------------
constexpr uint32_t TICK_MS = 20;                 // [sketch] 50 Hz
constexpr uint32_t BLE_TELEMETRY_MS = 200;       // [sketch] 5 Hz notify
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 2000;   // [new] the sketch left the watchdog as a comment

// ---- Sensors ---------------------------------------------------------------
constexpr int IR_WINDOW = 10;                    // [sketch] moving-average length
constexpr float TILT_THRESHOLD_DEG = 30.0f;      // [sketch]
constexpr uint32_t TILT_DEBOUNCE_MS = 500;       // [sketch] tilt must persist this long to count
constexpr uint32_t ACCEL_INTERVAL_MS = 50;       // [sketch]
constexpr int ADC_MAX = 4095;                    // [sketch] 12-bit
constexpr int THROTTLE_DEADZONE_ADC = 200;       // [sketch] about 5 % of range

// ---- Safety ----------------------------------------------------------------
constexpr uint32_t TILT_EMERGENCY_MS = 3000;     // [sketch] tilted this long while riding -> latched stop
constexpr uint32_t RED_ZONE_RAMP_MS = 3000;      // [sketch] controlled stop in a red zone
constexpr uint8_t SLOW_ZONE_CAP_PERCENT = 50;    // [sketch]
constexpr uint8_t MIN_BATTERY_PERCENT = 10;      // [book] pre-ride check, ch. 8.5.1

// ---- Throttle ramp, percent per 20 ms tick --------------------------------- [sketch]
constexpr uint8_t RAMP_UP_UNLOCKING = 1;
constexpr uint8_t RAMP_UP_RIDING = 2;
constexpr uint8_t RAMP_UP_WARNING = 1;
constexpr uint8_t RAMP_DOWN_RIDING = 3;
constexpr uint8_t RAMP_DOWN_WARNING = 4;
constexpr uint8_t RAMP_DOWN_EMERGENCY = 20;

// ---- Motor PWM -------------------------------------------------------------- [sketch]
constexpr uint32_t MOTOR_PWM_FREQ = 1000;
constexpr uint8_t MOTOR_PWM_BITS = 8;

// ---- Battery divider -------------------------------------------------------- [new]
// Assumes a 10-cell Li-ion pack (42 V full) behind a 100k / 8.2k divider.
// The desk prototype ran from USB and had no pack: these numbers are placeholders.
constexpr float ADC_REF_VOLTS = 3.3f;
constexpr float BATTERY_DIVIDER_RATIO = (100.0f + 8.2f) / 8.2f;
constexpr float BATTERY_EMPTY_VOLTS = 33.0f;
constexpr float BATTERY_FULL_VOLTS = 42.0f;

// ---- Lock servo ------------------------------------------------------------- [new]
constexpr int LOCK_SERVO_LOCKED_DEG = 0;
constexpr int LOCK_SERVO_OPEN_DEG = 90;

// ---- Cloud link ------------------------------------------------------------- [book] ch. 9
constexpr uint32_t CLOUD_PERIOD_RIDING_MS = 1000;   // 1 Hz while riding
constexpr uint32_t CLOUD_PERIOD_IDLE_MS = 30000;    // [new] parked scooters report slowly
constexpr uint32_t BACKOFF_BASE_MS = 1000;          // [new]
constexpr uint32_t BACKOFF_MAX_MS = 60000;          // [new]
constexpr int OUTBOX_SLOTS = 32;                    // [new] telemetry kept while offline (RAM, not flash)
constexpr int GPS_BAUD = 9600;                      // NEO-6M default

}  // namespace cfg
