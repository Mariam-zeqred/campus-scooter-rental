#include "CloudLink.h"

#include <Arduino.h>
#include <MQTT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Config.h"
#include "Link.h"
#include "Nmea.h"
#include "Telemetry.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h not found: building with the placeholders from secrets.example.h"
#include "secrets.example.h"
#endif

namespace cloud {

namespace {

constexpr int kRecordLength = 300;

scooter::IntentBox* intents = nullptr;
QueueHandle_t snapshotQueue = nullptr;  // length 1, overwritten by the control loop
QueueHandle_t alertQueue = nullptr;

WiFiClientSecure net;
MQTTClient mqtt(512);
scooter::NmeaReader gps;
scooter::Backoff wifiBackoff(cfg::BACKOFF_BASE_MS, cfg::BACKOFF_MAX_MS);
scooter::Backoff mqttBackoff(cfg::BACKOFF_BASE_MS, cfg::BACKOFF_MAX_MS);
scooter::Outbox<cfg::OUTBOX_SLOTS, kRecordLength> telemetryOutbox;
scooter::Outbox<4, 120> alertOutbox;

char telemetryTopicName[96];
char commandTopicName[96];
char alertTopicName[96];

uint32_t unixTime() {
  const time_t t = time(nullptr);
  return t > 1600000000 ? static_cast<uint32_t>(t) : 0;  // 0 until NTP has answered
}

void onMessage(String&, String& payload) {
  const scooter::Command cmd =
      scooter::parseCommand(payload.c_str(), payload.length(), scooter::Channel::Any);
  if (cmd != scooter::Command::None) intents->post(cmd);
}

// Keep Wi-Fi and MQTT up, retrying with a growing delay. Returns true when
// the broker session is usable.
bool maintainConnection(uint32_t now) {
  static uint32_t wifiTriedMs = 0, wifiWaitMs = 0, mqttTriedMs = 0, mqttWaitMs = 0;
  static bool clockStarted = false;

  if (WiFi.status() != WL_CONNECTED) {
    if (now - wifiTriedMs >= wifiWaitMs) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      wifiTriedMs = now;
      wifiWaitMs = wifiBackoff.nextDelayMs();
    }
    return false;
  }
  wifiBackoff.reset();
  wifiWaitMs = 0;
  if (!clockStarted) {
    configTime(0, 0, "pool.ntp.org");  // UTC; needed for timestamps
    clockStarted = true;
  }

  if (mqtt.connected()) {
    mqtt.loop();
    return true;
  }
  if (now - mqttTriedMs >= mqttWaitMs) {
    mqttTriedMs = now;
    if (mqtt.connect(DEVICE_ID)) {  // blocks during the TLS handshake
      mqttBackoff.reset();
      mqttWaitMs = 0;
      mqtt.subscribe(commandTopicName, 1);
      return true;
    }
    mqttWaitMs = mqttBackoff.nextDelayMs();
  }
  return false;
}

void task(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // reconnects are paced by wifiBackoff
  net.setCACert(AWS_ROOT_CA);
  net.setCertificate(DEVICE_CERT);
  net.setPrivateKey(DEVICE_KEY);
  mqtt.begin(MQTT_HOST, MQTT_PORT, net);
  mqtt.onMessage(onMessage);
  if (cfg::GPS_FITTED) Serial2.begin(cfg::GPS_BAUD, SERIAL_8N1, cfg::PIN_GPS_RX, cfg::PIN_GPS_TX);

  uint32_t lastRecordMs = 0;
  char buf[kRecordLength];

  for (;;) {
    const uint32_t now = millis();
    while (cfg::GPS_FITTED && Serial2.available()) gps.feed(static_cast<char>(Serial2.read()));

    const bool online = maintainConnection(now);

    // Alerts first: small, rare, and the ones that must arrive (QoS 1).
    scooter::Cause cause;
    while (xQueueReceive(alertQueue, &cause, 0) == pdTRUE) {
      if (scooter::buildAlert(DEVICE_ID, unixTime(), cause, buf, sizeof(buf))) alertOutbox.push(buf);
    }
    while (online && !alertOutbox.empty() && mqtt.publish(alertTopicName, alertOutbox.front(), false, 1)) {
      alertOutbox.pop();
    }

    // Routine telemetry: 1 Hz in a ride, slowly when parked. Every record goes
    // through the outbox, so the offline and online paths are the same code.
    Snapshot s;
    if (xQueuePeek(snapshotQueue, &s, 0) == pdTRUE) {
      const bool inRide = s.state != scooter::State::Locked;
      const uint32_t period = inRide ? cfg::CLOUD_PERIOD_RIDING_MS : cfg::CLOUD_PERIOD_IDLE_MS;
      if (lastRecordMs == 0 || now - lastRecordMs >= period) {
        lastRecordMs = now ? now : 1;
        scooter::CloudSample sample;
        sample.deviceId = DEVICE_ID;
        sample.timestamp = unixTime();
        sample.state = s.state;
        sample.zone = s.zone;
        sample.throttle = s.throttle;
        sample.gps = gps.fix();
        sample.batteryVolts = s.batteryVolts;
        sample.batteryPercent = s.batteryPercent;
        sample.rssi = WiFi.RSSI();
        sample.uptimeS = now / 1000;
        if (scooter::buildCloudRecord(sample, buf, sizeof(buf))) telemetryOutbox.push(buf);
      }
    }
    while (online && !telemetryOutbox.empty() && mqtt.publish(telemetryTopicName, telemetryOutbox.front())) {
      telemetryOutbox.pop();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

void begin(scooter::IntentBox& box) {
  intents = &box;
  scooter::telemetryTopic(ORG_ID, DEVICE_ID, telemetryTopicName, sizeof(telemetryTopicName));
  scooter::commandTopic(ORG_ID, DEVICE_ID, commandTopicName, sizeof(commandTopicName));
  scooter::alertTopic(ORG_ID, alertTopicName, sizeof(alertTopicName));
  snapshotQueue = xQueueCreate(1, sizeof(Snapshot));
  alertQueue = xQueueCreate(4, sizeof(scooter::Cause));
  xTaskCreatePinnedToCore(task, "cloud", 10240, nullptr, 1, nullptr, 0);  // core 0: the radio's core
}

void publishSnapshot(const Snapshot& s) {
  if (snapshotQueue) xQueueOverwrite(snapshotQueue, &s);
}

void raiseAlert(scooter::Cause cause) {
  if (alertQueue) xQueueSend(alertQueue, &cause, 0);
}

}  // namespace cloud
