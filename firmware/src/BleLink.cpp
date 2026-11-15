#include "BleLink.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <atomic>
#include <string.h>

#include "Telemetry.h"

namespace ble {

namespace {

// Fixed UUIDs from the sketch ("LOCK THESE - used by Flutter later").
const char* kDeviceName = "SMART_SCOOTER_001";
const char* kServiceUuid = "8b7a0001-6e6f-4f5a-bc9a-001001001001";
const char* kCommandUuid = "8b7a0002-6e6f-4f5a-bc9a-001001001001";
const char* kZoneUuid = "8b7a0003-6e6f-4f5a-bc9a-001001001001";
const char* kStatusUuid = "8b7a0004-6e6f-4f5a-bc9a-001001001001";

scooter::IntentBox* intents = nullptr;
BLEServer* server = nullptr;
BLECharacteristic* statusChar = nullptr;
std::atomic<bool> clientConnected{false};
std::atomic<bool> restartAdvertising{false};
uint32_t disconnectedAtMs = 0;

// These callbacks run on the Bluetooth task, not the control loop, so they
// only post into the IntentBox.
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { clientConnected = true; }
  void onDisconnect(BLEServer*) override {
    clientConnected = false;
    intents->post(scooter::Command::Lock);  // phone gone: ask to lock (as the sketch did)
    restartAdvertising = true;
  }
};

class WriteCallback : public BLECharacteristicCallbacks {
 public:
  explicit WriteCallback(scooter::Channel channel) : channel_(channel) {}
  void onWrite(BLECharacteristic* c) override {
    const auto value = c->getValue();  // String on core 3.x, std::string on 2.x
    const char* text = value.c_str();
    const scooter::Command cmd = scooter::parseCommand(text, strlen(text), channel_);
    if (cmd != scooter::Command::None) intents->post(cmd);
  }

 private:
  scooter::Channel channel_;
};

}  // namespace

void begin(scooter::IntentBox& box) {
  intents = &box;
  BLEDevice::init(kDeviceName);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(kServiceUuid);

  BLECharacteristic* command = service->createCharacteristic(kCommandUuid, BLECharacteristic::PROPERTY_WRITE);
  command->setCallbacks(new WriteCallback(scooter::Channel::Ride));

  BLECharacteristic* zone = service->createCharacteristic(kZoneUuid, BLECharacteristic::PROPERTY_WRITE);
  zone->setCallbacks(new WriteCallback(scooter::Channel::Zone));

  statusChar = service->createCharacteristic(
      kStatusUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());

  service->start();
  server->getAdvertising()->start();
}

bool connected() { return clientConnected; }

void notifyStatus(const scooter::Context& ctx) {
  char buf[96];
  if (scooter::buildBleStatus(ctx, buf, sizeof(buf)) == 0) return;
  statusChar->setValue(buf);
  statusChar->notify();
}

void poll(uint32_t nowMs) {
  if (restartAdvertising.exchange(false)) disconnectedAtMs = nowMs ? nowMs : 1;
  if (disconnectedAtMs != 0 && nowMs - disconnectedAtMs >= 100) {
    server->startAdvertising();
    disconnectedAtMs = 0;
  }
}

}  // namespace ble
