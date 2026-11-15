// Host tests for scooter_core. No hardware needed.
//   pio test -e native            (PlatformIO + Unity)
//   python tools/check.py         (same file, compiled with any host C++ compiler)
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "Battery.h"
#include "Commands.h"
#include "Config.h"
#include "Filters.h"
#include "Link.h"
#include "Nmea.h"
#include "Safety.h"
#include "StateMachine.h"
#include "Telemetry.h"

using namespace scooter;

#define ASSERT_STATE(expected, rig) TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((rig).c.state))

// Runs the control loop exactly as main.cpp does: rules, state machine, ramp.
struct Rig {
  Context c;
  SafetyController safety;
  uint32_t now = 0;

  void tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
      safety.evaluate(c, now);
      step(c);
      rampThrottle(c);
      now += cfg::TICK_MS;
    }
  }
  void unlockAndRide() {
    c.unlockRequested = true;
    tick(2);  // LOCKED -> UNLOCKING -> RIDING
  }
};

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- ride machine

void test_boots_locked_and_ignores_throttle() {
  Rig r;
  r.c.requestedThrottle = 100;
  r.tick(50);
  ASSERT_STATE(State::Locked, r);
  TEST_ASSERT_EQUAL_INT(0, r.c.motorThrottle);
  TEST_ASSERT_TRUE(lockEngaged(r.c.state));
  TEST_ASSERT_FALSE(motorEnabled(r.c.state));
}

void test_unlock_runs_the_safety_check_then_rides() {
  Rig r;
  r.c.unlockRequested = true;
  r.tick();
  ASSERT_STATE(State::Unlocking, r);
  r.tick();
  ASSERT_STATE(State::Riding, r);
  TEST_ASSERT_FALSE(lockEngaged(r.c.state));
}

void test_unlock_waits_while_unsafe() {
  Rig r;
  r.c.tilted = true;
  r.c.unlockRequested = true;
  r.tick(100);
  ASSERT_STATE(State::Unlocking, r);  // tilted: stays in the check
  r.c.tilted = false;
  r.c.obstacle = true;
  r.tick(100);
  ASSERT_STATE(State::Unlocking, r);  // obstacle in front
  r.c.obstacle = false;
  r.c.batteryPercent = cfg::MIN_BATTERY_PERCENT;
  r.tick(100);
  ASSERT_STATE(State::Unlocking, r);  // battery too low
  r.c.batteryPercent = 80;
  r.tick();
  ASSERT_STATE(State::Riding, r);
}

void test_machine_never_skips_states() {
  TEST_ASSERT_FALSE(transitionAllowed(State::Locked, State::Riding, false));
  TEST_ASSERT_FALSE(transitionAllowed(State::Locked, State::EmergencyStop, false));
  TEST_ASSERT_FALSE(transitionAllowed(State::Warning, State::Locked, false));
  TEST_ASSERT_FALSE(transitionAllowed(State::EmergencyStop, State::Locked, true));
  TEST_ASSERT_TRUE(transitionAllowed(State::EmergencyStop, State::Locked, false));
  Context c;
  c.requested = State::Riding;  // a rule asks for something illegal
  TEST_ASSERT_FALSE(step(c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(State::Locked), static_cast<int>(c.state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(State::Locked), static_cast<int>(c.requested));
}

void test_throttle_ramps_up_gently() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 100;
  r.tick(10);
  TEST_ASSERT_EQUAL_INT(10 * cfg::RAMP_UP_RIDING, r.c.motorThrottle);  // 2 % per tick
  r.tick(100);
  TEST_ASSERT_EQUAL_INT(100, r.c.motorThrottle);
}

void test_slow_zone_caps_speed() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 90;
  r.c.requestedZone = Zone::Slow;
  r.c.zoneUpdateRequested = true;
  r.tick(100);
  ASSERT_STATE(State::Riding, r);
  TEST_ASSERT_EQUAL_INT(cfg::SLOW_ZONE_CAP_PERCENT, r.c.motorThrottle);
}

void test_red_zone_is_a_controlled_stop() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 60;
  r.tick(60);
  TEST_ASSERT_EQUAL_INT(60, r.c.motorThrottle);

  r.c.requestedZone = Zone::Red;
  r.c.zoneUpdateRequested = true;
  r.tick();
  ASSERT_STATE(State::Warning, r);
  r.tick(40);  // 0.8 s in: slowing, not stopped
  TEST_ASSERT_TRUE(r.c.motorThrottle > 0 && r.c.motorThrottle < 60);
  r.tick(120);  // past the 3 s ramp
  TEST_ASSERT_EQUAL_INT(0, r.c.motorThrottle);

  r.c.requestedThrottle = 0;
  r.c.lockRequested = true;  // parking inside a red zone is refused
  r.tick();
  ASSERT_STATE(State::Warning, r);

  r.c.requestedZone = Zone::Clear;
  r.c.zoneUpdateRequested = true;
  r.tick(2);
  ASSERT_STATE(State::Riding, r);
}

void test_lock_needs_released_throttle() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 40;
  r.tick(30);
  r.c.lockRequested = true;
  r.tick();
  ASSERT_STATE(State::Riding, r);  // denied while the throttle is applied
  TEST_ASSERT_FALSE(r.c.lockRequested);  // and the request is dropped

  r.c.requestedThrottle = 0;
  r.tick();
  r.c.lockRequested = true;
  r.tick();
  ASSERT_STATE(State::Locked, r);
}

// ---------------------------------------------------------------- emergencies

void test_obstacle_is_a_soft_stop_that_resumes() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 80;
  r.tick(60);
  r.c.obstacle = true;
  r.tick();
  ASSERT_STATE(State::EmergencyStop, r);
  TEST_ASSERT_FALSE(r.c.emergencyLatched);
  r.tick(4);  // 20 % per tick: 80 -> 0 in four ticks after the first
  TEST_ASSERT_EQUAL_INT(0, r.c.motorThrottle);
  r.c.obstacle = false;
  r.tick();
  ASSERT_STATE(State::Riding, r);  // no RESET needed
}

void test_obstacle_inside_red_zone_stops_and_recovers() {
  // The prototype sketch ignored the obstacle in WARNING; this rewrite does not.
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 50;
  r.tick(40);
  r.c.requestedZone = Zone::Red;
  r.c.zoneUpdateRequested = true;
  r.tick(2);
  ASSERT_STATE(State::Warning, r);
  r.c.obstacle = true;
  r.tick();
  ASSERT_STATE(State::EmergencyStop, r);
  r.c.obstacle = false;
  r.tick(3);
  ASSERT_STATE(State::Warning, r);  // back in the red-zone stop, not stuck
}

void test_long_tilt_latches_until_reset() {
  Rig r;
  r.unlockAndRide();
  r.c.requestedThrottle = 50;
  r.tick(30);
  r.c.tilted = true;
  r.tick(cfg::TILT_EMERGENCY_MS / cfg::TICK_MS);  // just under 3 s
  ASSERT_STATE(State::Riding, r);
  r.tick(2);
  ASSERT_STATE(State::EmergencyStop, r);
  TEST_ASSERT_TRUE(r.c.emergencyLatched);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Cause::Tilt), static_cast<int>(r.c.cause));

  r.c.tilted = false;  // upright again, but nobody pressed reset
  r.tick(200);
  ASSERT_STATE(State::EmergencyStop, r);

  r.c.resetRequested = true;  // reset with the throttle still applied: refused
  r.tick(10);
  ASSERT_STATE(State::EmergencyStop, r);

  r.c.requestedThrottle = 0;  // the pending reset goes through once it is safe
  r.tick(2);
  ASSERT_STATE(State::Locked, r);
  TEST_ASSERT_FALSE(r.c.emergencyLatched);
}

void test_short_tilt_and_parked_tilt_are_ignored() {
  Rig r;
  r.c.tilted = true;  // lying on its side while locked
  r.tick(400);
  ASSERT_STATE(State::Locked, r);
  TEST_ASSERT_FALSE(r.c.emergencyLatched);

  r.c.tilted = false;
  r.unlockAndRide();
  r.c.tilted = true;  // a 1 s lean while riding
  r.tick(50);
  r.c.tilted = false;
  r.tick(200);
  ASSERT_STATE(State::Riding, r);
}

void test_stray_reset_does_not_clear_a_later_emergency() {
  // In the sketch a RESET sent while nothing was wrong stayed pending.
  Rig r;
  r.unlockAndRide();
  r.c.resetRequested = true;
  r.tick(5);
  TEST_ASSERT_FALSE(r.c.resetRequested);
  r.c.tilted = true;
  r.tick(cfg::TILT_EMERGENCY_MS / cfg::TICK_MS + 2);
  r.c.tilted = false;
  r.tick(100);
  ASSERT_STATE(State::EmergencyStop, r);
}

// ---------------------------------------------------------------- sensors

void test_ir_filter_needs_a_majority() {
  IrFilter f;
  for (int i = 0; i < 5; ++i) TEST_ASSERT_FALSE(f.update(0));  // 5 of 10 low: not yet
  TEST_ASSERT_TRUE(f.update(0));                                // 6 of 10
  for (int i = 0; i < 5; ++i) f.update(1);
  TEST_ASSERT_FALSE(f.update(1));
}

void test_tilt_angle_and_debounce() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, tiltAngleDeg(0, 0, 9.81f));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 90.0f, tiltAngleDeg(9.81f, 0, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 45.0f, tiltAngleDeg(0, 5.0f, 5.0f));
  TiltDebounce d;
  TEST_ASSERT_FALSE(d.update(60, 0));
  TEST_ASSERT_FALSE(d.update(60, 400));
  TEST_ASSERT_TRUE(d.update(60, 500));
  TEST_ASSERT_FALSE(d.update(5, 520));   // clears at once
  TEST_ASSERT_FALSE(d.update(60, 540));  // and the timer starts again
}

void test_throttle_dead_zone_and_range() {
  TEST_ASSERT_EQUAL_INT(0, throttlePercentFromAdc(0));
  TEST_ASSERT_EQUAL_INT(0, throttlePercentFromAdc(cfg::THROTTLE_DEADZONE_ADC - 1));
  TEST_ASSERT_EQUAL_INT(0, throttlePercentFromAdc(cfg::THROTTLE_DEADZONE_ADC));
  TEST_ASSERT_EQUAL_INT(50, throttlePercentFromAdc(2148));
  TEST_ASSERT_EQUAL_INT(100, throttlePercentFromAdc(4095));
  TEST_ASSERT_EQUAL_INT(100, throttlePercentFromAdc(5000));
}

void test_battery_divider_and_gauge() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, batteryVoltsFromAdc(0));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.3f * cfg::BATTERY_DIVIDER_RATIO, batteryVoltsFromAdc(4095));
  TEST_ASSERT_EQUAL_INT(0, batteryPercentFromVolts(cfg::BATTERY_EMPTY_VOLTS - 1));
  TEST_ASSERT_EQUAL_INT(100, batteryPercentFromVolts(cfg::BATTERY_FULL_VOLTS + 1));
  TEST_ASSERT_EQUAL_INT(50, batteryPercentFromVolts(37.5f));
  BatteryGauge g;
  g.update(3600);
  const float first = g.volts();
  g.update(0);  // one wild sample moves the reading by 5 %, not to zero
  TEST_ASSERT_FLOAT_WITHIN(0.01f, first * 0.95f, g.volts());
}

// ---------------------------------------------------------------- GPS

static void withChecksum(const char* body, char* out, size_t size) {
  unsigned char sum = 0;
  for (const char* p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
  snprintf(out, size, "$%s*%02X", body, sum);
}

void test_nmea_parses_a_valid_fix() {
  GpsFix fix;
  TEST_ASSERT_TRUE(parseRmc("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", fix));
  TEST_ASSERT_TRUE(fix.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 48.1173f, static_cast<float>(fix.lat));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 11.5167f, static_cast<float>(fix.lng));

  char s[100];
  withChecksum("GNRMC,010203,A,3345.000,S,07030.000,W,0.0,0.0,010126,,", s, sizeof(s));
  TEST_ASSERT_TRUE(parseRmc(s, fix));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -33.75f, static_cast<float>(fix.lat));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -70.5f, static_cast<float>(fix.lng));
}

void test_nmea_rejects_bad_input() {
  GpsFix fix;
  char s[100];
  TEST_ASSERT_FALSE(parseRmc("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B", fix));
  withChecksum("GPRMC,123519,V,,,,,,,230394,,", s, sizeof(s));  // receiver has no fix
  TEST_ASSERT_FALSE(parseRmc(s, fix));
  withChecksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", s, sizeof(s));
  TEST_ASSERT_FALSE(parseRmc(s, fix));  // not an RMC sentence
  TEST_ASSERT_FALSE(parseRmc("garbage", fix));
  TEST_ASSERT_FALSE(fix.valid);
}

void test_nmea_reader_assembles_lines() {
  NmeaReader reader;
  const char* stream = "noise$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
  bool got = false;
  for (const char* p = stream; *p; ++p) got = reader.feed(*p) || got;
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 48.1173f, static_cast<float>(reader.fix().lat));
}

// ---------------------------------------------------------------- messages

void test_ble_status_matches_the_sketch_format() {
  Context c;
  c.state = State::Riding;
  c.zone = Zone::Slow;
  c.motorThrottle = 40;
  c.batteryPercent = 86;
  char buf[96];
  TEST_ASSERT_TRUE(buildBleStatus(c, buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"state\":\"RIDING\",\"zone\":\"SLOW\",\"throttle\":40,\"battery\":86}", buf);
  c.state = State::EmergencyStop;
  buildBleStatus(c, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "\"state\":\"EMERGENCY\"") != nullptr);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(buildBleStatus(c, buf, 10)));  // too small: reported, not truncated
}

void test_cloud_record_and_topics() {
  CloudSample s;
  s.deviceId = "SC-0001";
  s.timestamp = 1767225600;
  s.state = State::Riding;
  s.throttle = 35;
  s.gps.valid = true;
  s.gps.lat = 31.040948;
  s.gps.lng = 31.378470;
  s.batteryVolts = 38.24f;
  s.batteryPercent = 58;
  s.rssi = -65;
  s.uptimeS = 12044;
  char buf[320];
  TEST_ASSERT_TRUE(buildCloudRecord(s, buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"device_id\":\"SC-0001\",\"timestamp\":1767225600,\"state\":\"RIDING\",\"zone\":\"CLEAR\","
      "\"throttle\":35,\"gps\":{\"lat\":31.040948,\"lng\":31.378470},"
      "\"battery\":{\"voltage\":38.2,\"percent\":58},\"diagnostics\":{\"rssi\":-65,\"uptime\":12044}}",
      buf);
  s.gps.valid = false;
  buildCloudRecord(s, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "\"gps\":null") != nullptr);

  buildAlert("SC-0001", 1767225600, Cause::Tilt, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\"device_id\":\"SC-0001\",\"timestamp\":1767225600,\"alert\":\"TILT\"}", buf);

  telemetryTopic("campus-demo", "SC-0001", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("campus/campus-demo/scooter/SC-0001/telemetry", buf);
  commandTopic("campus-demo", "SC-0001", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("campus/campus-demo/scooter/SC-0001/cmd", buf);
  alertTopic("campus-demo", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("campus/campus-demo/alerts/critical", buf);
}

void test_commands_parse_per_channel() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::Unlock), static_cast<int>(parseCommand("UNLOCK\n", 7, Channel::Ride)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::Lock), static_cast<int>(parseCommand("  LOCK ", 7, Channel::Any)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::ZoneRed), static_cast<int>(parseCommand("RED", 3, Channel::Zone)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::None), static_cast<int>(parseCommand("RED", 3, Channel::Ride)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::None), static_cast<int>(parseCommand("UNLOCK", 6, Channel::Zone)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::None), static_cast<int>(parseCommand("unlock", 6, Channel::Any)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::None), static_cast<int>(parseCommand("", 0, Channel::Any)));
}

void test_intent_box_hands_commands_to_the_loop() {
  IntentBox box;
  Context c;
  box.post(Command::Unlock);
  box.post(Command::ZoneSlow);
  box.post(Command::ZoneRed);  // the latest zone wins
  box.drainInto(c);
  TEST_ASSERT_TRUE(c.unlockRequested);
  TEST_ASSERT_TRUE(c.zoneUpdateRequested);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Zone::Red), static_cast<int>(c.requestedZone));
  Context d;
  box.drainInto(d);  // drained once: nothing left
  TEST_ASSERT_FALSE(d.unlockRequested || d.zoneUpdateRequested);
}

// ---------------------------------------------------------------- link helpers

void test_backoff_doubles_to_a_ceiling() {
  Backoff b(1000, 60000);
  TEST_ASSERT_EQUAL_INT(1000, b.nextDelayMs());
  TEST_ASSERT_EQUAL_INT(2000, b.nextDelayMs());
  TEST_ASSERT_EQUAL_INT(4000, b.nextDelayMs());
  for (int i = 0; i < 10; ++i) b.nextDelayMs();
  TEST_ASSERT_EQUAL_INT(60000, b.nextDelayMs());
  b.reset();
  TEST_ASSERT_EQUAL_INT(1000, b.nextDelayMs());
}

void test_outbox_keeps_the_newest_records() {
  Outbox<3, 16> box;
  TEST_ASSERT_TRUE(box.empty());
  TEST_ASSERT_FALSE(box.push("this text is far too long for a slot"));
  box.push("a");
  box.push("b");
  box.push("c");
  box.push("d");  // full: "a" is dropped
  TEST_ASSERT_EQUAL_INT(3, box.size());
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(box.dropped()));
  TEST_ASSERT_EQUAL_STRING("b", box.front());
  box.pop();
  TEST_ASSERT_EQUAL_STRING("c", box.front());
  box.pop();
  box.pop();
  TEST_ASSERT_TRUE(box.empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boots_locked_and_ignores_throttle);
  RUN_TEST(test_unlock_runs_the_safety_check_then_rides);
  RUN_TEST(test_unlock_waits_while_unsafe);
  RUN_TEST(test_machine_never_skips_states);
  RUN_TEST(test_throttle_ramps_up_gently);
  RUN_TEST(test_slow_zone_caps_speed);
  RUN_TEST(test_red_zone_is_a_controlled_stop);
  RUN_TEST(test_lock_needs_released_throttle);
  RUN_TEST(test_obstacle_is_a_soft_stop_that_resumes);
  RUN_TEST(test_obstacle_inside_red_zone_stops_and_recovers);
  RUN_TEST(test_long_tilt_latches_until_reset);
  RUN_TEST(test_short_tilt_and_parked_tilt_are_ignored);
  RUN_TEST(test_stray_reset_does_not_clear_a_later_emergency);
  RUN_TEST(test_ir_filter_needs_a_majority);
  RUN_TEST(test_tilt_angle_and_debounce);
  RUN_TEST(test_throttle_dead_zone_and_range);
  RUN_TEST(test_battery_divider_and_gauge);
  RUN_TEST(test_nmea_parses_a_valid_fix);
  RUN_TEST(test_nmea_rejects_bad_input);
  RUN_TEST(test_nmea_reader_assembles_lines);
  RUN_TEST(test_ble_status_matches_the_sketch_format);
  RUN_TEST(test_cloud_record_and_topics);
  RUN_TEST(test_commands_parse_per_channel);
  RUN_TEST(test_intent_box_hands_commands_to_the_loop);
  RUN_TEST(test_backoff_doubles_to_a_ceiling);
  RUN_TEST(test_outbox_keeps_the_newest_records);
  return UNITY_END();
}
