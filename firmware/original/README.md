# The surviving prototype sketch

`scooter_2026-01-25.ino` is a copy of the Arduino sketch found on the author's
PC (file date 25 January 2026), unchanged. It is the only firmware file from
the original project that survived, and it is kept here as the reference the
rewrite in `../src` and `../lib/scooter_core` was ported from.

It is not built by PlatformIO (it sits outside `src/`). To run it as it was,
open it in the Arduino IDE with the ESP32 board package 3.x and the libraries
Adafruit ADXL345, Adafruit Unified Sensor and LiquidCrystal_I2C.

What it contains: the five-state ride machine, the safety controller, the
state-dependent throttle ramp, the IR / tilt / throttle filters, the LCD and
buzzer feedback and the BLE service (commands, zone, status). What it does not
contain: Wi-Fi, MQTT, GPS, battery measurement, a lock actuator or a watchdog.
Whether this exact revision is the one shown at the project defence is not
known; its pin numbers differ from the pin map printed in the book
(see `docs/HARDWARE.md`).
