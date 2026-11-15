#include <Wire.h>
#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Sensor.h>
#include <LiquidCrystal_I2C.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BUZZER_PIN 25
#define MOTOR_PWM_PIN 26
#define MOTOR_PWM_CHANNEL 0
#define MOTOR_PWM_FREQ 1000
#define MOTOR_PWM_RESOLUTION 8
#define IR_PIN 27
#define POT_PIN 34

BLEServer* bleServer;
BLEService* bleService;

BLECharacteristic* commandChar;
BLECharacteristic* zoneChar;
BLECharacteristic* telemetryChar;


unsigned long lastTelemetryMs = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 200; // 5 Hz
bool telemetryEnabled = false;
bool bleClientConnected = false;


unsigned long lastDebugPrint = 0;
const unsigned long DEBUG_INTERVAL_MS = 200; // 5 Hz
unsigned long tiltStartTime = 0;
const unsigned long TILT_EMERGENCY_MS = 3000;


// ---- Throttle ramping ----
const uint8_t THROTTLE_RAMP_UP_PER_TICK   = 1;  // % per 20ms → ~1.5s 0→100
const uint8_t THROTTLE_RAMP_DOWN_PER_TICK = 1;  // faster braking


// ---- State-aware throttle ramps (% per tick @ 50Hz) ----

// Acceleration
const uint8_t RAMP_UP_UNLOCKING = 1;   // very gentle
const uint8_t RAMP_UP_RIDING    = 2;   // normal accel
const uint8_t RAMP_UP_WARNING   = 1;   // cautious accel

// Braking
const uint8_t RAMP_DOWN_RIDING  = 3;   // normal brake
const uint8_t RAMP_DOWN_WARNING = 4;   // stronger brake
const uint8_t RAMP_DOWN_EMERG   = 20;  // HARD stop (almost instant)



#define SCOOTER_ID "SMART_SCOOTER_001"

// BLE UUIDs (LOCK THESE – used by Flutter later)
#define SCOOTER_SERVICE_UUID  "8b7a0001-6e6f-4f5a-bc9a-001001001001"
#define COMMAND_CHAR_UUID    "8b7a0002-6e6f-4f5a-bc9a-001001001001"
#define ZONE_CHAR_UUID       "8b7a0003-6e6f-4f5a-bc9a-001001001001"
#define TELEMETRY_CHAR_UUID  "8b7a0004-6e6f-4f5a-bc9a-001001001001"

class ZoneCallback;
class CommandCallback;




enum SystemState {
  LOCKED,
  UNLOCKING,
  RIDING,
  WARNING,
  EMERGENCY_STOP
};

struct SystemContext {
  // ---- State ----
  SystemState currentState = LOCKED;

  // ---- Requested transition (by Safety / BLE) ----
  SystemState requestedState = LOCKED;


  // ---- Safety flags (set by SensorsManager) ----
  bool isTilted = false;
  bool obstacleDetected = false;
  bool emergencyLatched = false;

  // ---- User input (processed) ----
  uint8_t throttlePercent = 0;   // 0–100%

  // ---- System info ----
  uint8_t batteryPercent = 100;  // simulated

  enum EmergencyCause {
  NONE,
  OBSTACLE,
  TILT,
  THEFT,
  GEOFENCE
  };

  EmergencyCause emergencyCause = NONE;

  enum ZoneType {
    ZONE_CLEAR,   // normal riding
    ZONE_SLOW,    // speed capped
    ZONE_RED      // controlled stop
  };

  ZoneType currentZone = ZONE_CLEAR;

  // ---- Throttle pipeline ----
  uint8_t requestedThrottle = 0;  // from user (potentiometer)
  uint8_t allowedThrottle   = 0;  // after safety + zone logic
  uint8_t motorThrottle     = 0;  // 🔵 ACTUAL applied throttle (PWM input)


 



  // ---- Red-zone gradual stop ----
  bool redZoneActive = false;
  uint8_t redZoneStartThrottle = 0;
  unsigned long redZoneStartTime = 0;

  // ---- User / BLE intents ----
  bool lockRequested = false;
  bool unlockRequested = false;

  // ---- BLE zone intent ----
  bool zoneUpdateRequested = false;
  SystemContext::ZoneType requestedZone = ZONE_CLEAR;


  bool emergencyResetRequested = false;



};

class SensorsManager {
public:
  void begin() {
    // ---- I2C ----
    Wire.begin(21, 22);

    accelAvailable = accel.begin();
    if (accelAvailable) {
      accel.setRange(ADXL345_RANGE_8_G);
      Serial.println("ADXL345 available");
    } else {
      Serial.println("ADXL345 unavailable (continuing safely)");
    }


    // ---- IR ----
    pinMode(IR_PIN, INPUT);

    // ---- Potentiometer ----
    pinMode(POT_PIN, INPUT);

    // ---- Init IR buffer ----
    for (int i = 0; i < IR_WINDOW; i++)
      irBuffer[i] = 1;

    bufferIndex = 0;
    lastTiltTime = 0;
    tiltActive = false;
    //lastMPURead = 0;
  }

  void update(SystemContext &ctx) {
    updateIR(ctx);          // ALWAYS RUNS
    updateThrottle(ctx);    // ALWAYS RUNS
    updateTiltNonBlocking(ctx); // CONDITIONAL
  }

private:
  // ================= ADXL345 =================
  Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
  bool accelAvailable = false;

  unsigned long lastAccelRead = 0;

  unsigned long lastTiltTime = 0;
  bool tiltActive = false;

  const unsigned long ACCEL_INTERVAL_MS = 50;
  const float TILT_THRESHOLD_DEG = 30.0;
  const unsigned long TILT_DEBOUNCE_MS = 500;


    
  void updateTiltNonBlocking(SystemContext &ctx) {
    if (!accelAvailable)
      return;

    unsigned long now = millis();
    if (now - lastAccelRead < ACCEL_INTERVAL_MS)
      return;

    lastAccelRead = now;

    sensors_event_t event;
    accel.getEvent(&event);

    float ax = event.acceleration.x;
    float ay = event.acceleration.y;
    float az = event.acceleration.z;

    float tiltAngle =
      atan2(sqrt(ax * ax + ay * ay), az) * 180.0 / PI;

    if (tiltAngle > TILT_THRESHOLD_DEG) {
      if (!tiltActive) {
        if (lastTiltTime == 0)
          lastTiltTime = now;

        if (now - lastTiltTime >= TILT_DEBOUNCE_MS) {
          tiltActive = true;
          ctx.isTilted = true;
        }
      }
    } else {
      lastTiltTime = 0;
      tiltActive = false;
      ctx.isTilted = false;
    }
  }

  // ================= IR SENSOR =================
  static const int IR_WINDOW = 10;
  int irBuffer[IR_WINDOW];
  int bufferIndex = 0;

  void updateIR(SystemContext &ctx) {
    int raw = digitalRead(IR_PIN); // ACTIVE LOW

    irBuffer[bufferIndex] = raw;
    bufferIndex = (bufferIndex + 1) % IR_WINDOW;

    int sum = 0;
    for (int i = 0; i < IR_WINDOW; i++)
      sum += irBuffer[i];

    float avg = (float)sum / IR_WINDOW;
    ctx.obstacleDetected = (avg < 0.5);
  }

  // ================= THROTTLE =================
  const int ADC_MAX = 4095;
  const int DEADZONE_ADC = 200;

  void updateThrottle(SystemContext &ctx) {
    int raw = analogRead(POT_PIN);

    if (raw < DEADZONE_ADC) {
      ctx.requestedThrottle = 0;
      return;
    }

    int mapped = map(raw, DEADZONE_ADC, ADC_MAX, 0, 100);
    ctx.requestedThrottle = constrain(mapped, 0, 100);
  }

};

void debugPrint(const SystemContext &ctx) {
  Serial.print("STATE: ");
  switch (ctx.currentState) {
    case LOCKED: Serial.print("LOCKED"); break;
    case UNLOCKING: Serial.print("UNLOCKING"); break;
    case RIDING: Serial.print("RIDING"); break;
    case WARNING: Serial.print("WARNING"); break;
    case EMERGENCY_STOP: Serial.print("EMERGENCY_STOP"); break;
  }

  Serial.print(" | Tilt: ");
  Serial.print(ctx.isTilted);

  Serial.print(" | Obstacle: ");
  Serial.print(ctx.obstacleDetected);

  Serial.print(" | ReqThr: ");
  Serial.print(ctx.requestedThrottle);

  Serial.print(" | AllowThr: ");
  Serial.print(ctx.allowedThrottle);

  Serial.print(" | MotorThr: ");
  Serial.print(ctx.motorThrottle);


  Serial.print(" | Battery: ");
  Serial.print(ctx.batteryPercent);
  Serial.println("%");






}




class SafetyController {
public:

  bool canRecoverFromEmergency(const SystemContext &ctx) {
    return
      !ctx.isTilted &&
      !ctx.obstacleDetected &&
      ctx.requestedThrottle == 0 &&
      ctx.currentZone != SystemContext::ZONE_RED;
  }


  void evaluate(SystemContext &ctx) {

    // ---- HARD SAFETY ALWAYS FIRST ----
    if (ctx.emergencyLatched) {

      ctx.allowedThrottle = 0;
      ctx.requestedState = EMERGENCY_STOP;

      // ----- Check for recovery -----
      if (ctx.emergencyResetRequested &&
          canRecoverFromEmergency(ctx)) {

        Serial.println("[SAFETY] Emergency cleared");

        ctx.emergencyLatched = false;
        ctx.emergencyCause = SystemContext::NONE;
        ctx.emergencyResetRequested = false;

        ctx.requestedState = LOCKED;   // 🔒 safe re-entry
      }

      return;
    }


    handleTilt(ctx);
    handleObstacle(ctx);
    handleLockUnlock(ctx);
    handleZoneUpdate(ctx);

    // ---- POLICY & CONTROL ----
    applyZoneRules(ctx);

    // ---------- UNLOCKING AUTO-COMPLETE ----------
    if (ctx.currentState == UNLOCKING) {

      bool safeToRide =
        !ctx.isTilted &&
        !ctx.obstacleDetected &&
        !ctx.emergencyLatched &&
        ctx.currentZone != SystemContext::ZONE_RED;

      if (safeToRide) {
        ctx.requestedState = RIDING;
        Serial.println("[POLICY] Unlock complete → RIDING");
      }
    }

  }


  void handleLockUnlock(SystemContext &ctx) {

     // ---------- UNLOCK ----------
    if (ctx.unlockRequested) {
      if (ctx.currentState == LOCKED) {
        ctx.requestedState = UNLOCKING;
        Serial.println("[POLICY] Unlock accepted");
      }
      ctx.unlockRequested = false;
    }


    // ---------- LOCK ----------
    if (ctx.lockRequested) {

      bool speedZero = (ctx.allowedThrottle == 0);
      bool notInRedZone = (ctx.currentZone != SystemContext::ZONE_RED);
      bool notEmergency = (ctx.currentState != EMERGENCY_STOP);

      if (speedZero && notInRedZone && notEmergency) {
        ctx.requestedState = LOCKED;
        Serial.println("[POLICY] Lock accepted");
      } else {
        Serial.println("[POLICY] Lock denied");
      }

      ctx.lockRequested = false;
    }
  }



  void handleZoneUpdate(SystemContext &ctx) {

  if (!ctx.zoneUpdateRequested)
    return;

  // Prevent zone changes during hard emergency
  if (ctx.emergencyLatched) {
    Serial.println("[ZONE] Update denied (emergency)");
    ctx.zoneUpdateRequested = false;
    return;
  }

  // Apply the zone
  ctx.currentZone = ctx.requestedZone;

  // If RED zone arrives while riding → enforce WARNING
  if (ctx.currentZone == SystemContext::ZONE_RED &&
      ctx.currentState == RIDING) {
    ctx.requestedState = WARNING;
  }

  Serial.print("[ZONE] Zone set to ");
  switch (ctx.currentZone) {
    case SystemContext::ZONE_CLEAR: Serial.println("CLEAR"); break;
    case SystemContext::ZONE_SLOW:  Serial.println("SLOW"); break;
    case SystemContext::ZONE_RED:   Serial.println("RED"); break;
  }

  ctx.zoneUpdateRequested = false;
  }




  void applyZoneRules(SystemContext &ctx) {
    // Exit red zone cleanup
    if (ctx.currentZone != SystemContext::ZONE_RED && ctx.redZoneActive) {

      ctx.redZoneActive = false;
      ctx.redZoneStartTime = 0;

      // Resume normal riding
      if (ctx.currentState == WARNING) {
        ctx.requestedState = RIDING;
      }
    }


    switch (ctx.currentZone) {

      case SystemContext::ZONE_CLEAR:
        ctx.allowedThrottle = ctx.requestedThrottle;
        break;

      case SystemContext::ZONE_SLOW:
        ctx.allowedThrottle = min(ctx.requestedThrottle, (uint8_t)50);
        break;

      case SystemContext::ZONE_RED: {

        ctx.requestedState = WARNING;

        if (!ctx.redZoneActive) {
          ctx.redZoneActive = true;
          ctx.redZoneStartTime = millis();
          ctx.redZoneStartThrottle = ctx.motorThrottle;

        }

        unsigned long elapsed = millis() - ctx.redZoneStartTime;

        if (elapsed >= RED_ZONE_RAMP_MS) {
          ctx.allowedThrottle = 0;
        } else {
          float ratio = 1.0f - (float)elapsed / RED_ZONE_RAMP_MS;
          ctx.allowedThrottle = (uint8_t)(ctx.redZoneStartThrottle * ratio);
        }

        break;
      }


    }
  // Final authority
  if (ctx.currentState != RIDING && ctx.currentState != WARNING) {
    ctx.allowedThrottle = 0;
  }
}


private:
  unsigned long tiltStartTime = 0;
  const unsigned long TILT_EMERGENCY_MS = 3000;
  const unsigned long RED_ZONE_RAMP_MS = 3000; // 3 seconds to stop



  void handleTilt(SystemContext &ctx) {

    // ✅ Ignore tilt unless scooter is active
    if (ctx.currentState != RIDING &&
        ctx.currentState != WARNING) {
      tiltStartTime = 0;
      return;
    }

    if (ctx.isTilted) {
      if (tiltStartTime == 0)
        tiltStartTime = millis();

      if (millis() - tiltStartTime >= TILT_EMERGENCY_MS) {
        Serial.println("[SAFETY] HARD EMERGENCY: TILT");
        ctx.emergencyLatched = true;
        ctx.emergencyCause = SystemContext::TILT;
        ctx.requestedState = EMERGENCY_STOP;
      }
    } else {
      tiltStartTime = 0;
    }
  }



  void handleObstacle(SystemContext &ctx) {
    if (ctx.currentState == RIDING && ctx.obstacleDetected) {
      Serial.println("[SAFETY] SOFT EMERGENCY: OBSTACLE");
      ctx.emergencyCause = SystemContext::OBSTACLE;
      ctx.requestedState = EMERGENCY_STOP;
    }

    if (ctx.currentState == EMERGENCY_STOP &&
        ctx.emergencyCause == SystemContext::OBSTACLE &&
        !ctx.obstacleDetected) {

      Serial.println("[SAFETY] Obstacle cleared");
      ctx.emergencyCause = SystemContext::NONE;
      ctx.requestedState = RIDING;
    }
  }


};



class UIManager {
public:
  void begin() {
    lcd.init();
    lcd.backlight();

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    lastBuzzToggle = 0;
    buzzerState = false;

    lastState = LOCKED;
    lastBattery = 255;   // invalid value to force first update
    lastThrottle = 255;

    showBootScreen();
  }

  void update(const SystemContext &ctx) {

    // --- Detect state change ---
    if (ctx.currentState != prevUiState) {

      // Reset buzzer timing on state entry
      lastBuzzToggle = 0;
      buzzerState = false;

      // Reset emergency beep latch on state entry
      if (ctx.currentState != EMERGENCY_STOP) {
        emergencyClearedBeeped = false;
      }

      prevUiState = ctx.currentState;
    }

    // Existing emergency UI logic
    if (ctx.currentState == EMERGENCY_STOP &&
        ctx.emergencyLatched &&
        ctx.emergencyResetRequested) {

      emergencyUiMode = EMERGENCY_RESET_BLOCKED;
      emergencyUiTimestamp = millis();
    }

    if (emergencyUiMode == EMERGENCY_RESET_BLOCKED &&
        millis() - emergencyUiTimestamp > 2000) {

      emergencyUiMode = EMERGENCY_NORMAL;
    }

    updateLCD(ctx);
    updateBuzzer(ctx);
  }


private:

  SystemState prevUiState = LOCKED;

  bool emergencyClearedBeeped = false;


  enum EmergencyUiMode {
    EMERGENCY_NORMAL,
    EMERGENCY_RESET_BLOCKED
  };

  EmergencyUiMode emergencyUiMode = EMERGENCY_NORMAL;
  unsigned long emergencyUiTimestamp = 0;



  // ---------- LCD ----------
  LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2);

  SystemState lastState;
  uint8_t lastBattery;
  uint8_t lastThrottle;

  void showBootScreen() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Smart Scooter");
    lcd.setCursor(0, 1);
    lcd.print("Booting...");
  }

  void updateLCD(const SystemContext &ctx) {

    // ---------- State change ----------
    if (ctx.currentState != lastState) {
      lcd.clear();
      drawStaticScreen(ctx);
      lastState = ctx.currentState;

      // force refresh of dynamic fields
      lastBattery = 255;
      lastThrottle = 255;
    }

    // ---------- Battery update ----------
    if (ctx.batteryPercent != lastBattery) {
      drawBattery(ctx.batteryPercent);
      lastBattery = ctx.batteryPercent;
    }

    // ---------- Throttle update ----------
    if (ctx.currentState == RIDING &&
    ctx.motorThrottle != lastThrottle) {

      drawThrottle(ctx.motorThrottle);
      lastThrottle = ctx.motorThrottle;
    }

  }

  void drawStaticScreen(const SystemContext &ctx) {
    switch (ctx.currentState) {

      case LOCKED:
        lcd.setCursor(0, 0);
        lcd.print("LOCKED");
        lcd.setCursor(0, 1);
        lcd.print("Scan to Unlock");
        break;

      case UNLOCKING:
        lcd.setCursor(0, 0);
        lcd.print("UNLOCKING");
        lcd.setCursor(0, 1);
        lcd.print("Safety Check");
        break;

      case RIDING:
        lcd.setCursor(0, 0);
        lcd.print("RIDING");
        lcd.setCursor(0, 1);
        lcd.print("THR: ");
        break;

      case WARNING:
        lcd.setCursor(0, 0);
        lcd.print("WARNING");

        lcd.setCursor(0, 1);

        if (ctx.currentZone == SystemContext::ZONE_RED) {
          lcd.print("Restricted Area");
        } 
        else if (ctx.emergencyCause == SystemContext::OBSTACLE) {
          lcd.print("Obstacle Ahead");
        } 
        else {
          lcd.print("Proceed w/ Care");
        }
        break;



    case EMERGENCY_STOP:
      lcd.setCursor(0, 0);

      if (emergencyUiMode == EMERGENCY_RESET_BLOCKED) {
        lcd.print("RESET BLOCKED");
        lcd.setCursor(0, 1);
        lcd.print("Fix Conditions");
        break;
      }

      lcd.print("EMERGENCY");
      lcd.setCursor(0, 1);

      switch (ctx.emergencyCause) {
        case SystemContext::TILT:
          lcd.print("Scooter Tilted");
          break;

        case SystemContext::OBSTACLE:
          lcd.print("Obstacle Block");
          break;

        default:
          lcd.print("System Halted");
          break;
      }
      break;

    }
  }

  void drawBattery(uint8_t battery) {
    lcd.setCursor(11, 0);   // right side of line 1
    lcd.print("    ");      // clear old
    lcd.setCursor(11, 0);
    lcd.print(battery);
    lcd.print("%");
  }

  void drawThrottle(uint8_t throttle) {
    lcd.setCursor(5, 1);    // after "THR: "
    lcd.print("    ");
    lcd.setCursor(5, 1);
    lcd.print(throttle);
    lcd.print("%");
  }

  // ---------- BUZZER ----------
  unsigned long lastBuzzToggle;
  bool buzzerState;

  void updateBuzzer(const SystemContext &ctx) {
    unsigned long now = millis();

    switch (ctx.currentState) {

      case WARNING:
        if (now - lastBuzzToggle >= 500) {
          lastBuzzToggle = now;
          buzzerState = !buzzerState;
          digitalWrite(BUZZER_PIN, buzzerState);
        }
        break;




      case EMERGENCY_STOP:

        if (ctx.emergencyLatched) {
          digitalWrite(BUZZER_PIN, HIGH);
          emergencyClearedBeeped = false;
        }
        else {
          // Emergency just cleared → confirmation beeps
          if (!emergencyClearedBeeped) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            delay(100);
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);

            emergencyClearedBeeped = true;
          }
        }
        break;





      default:
        digitalWrite(BUZZER_PIN, LOW);
        buzzerState = false;
        break;
    }
  }
};






void updateStateMachine(SystemContext &ctx) {

  SystemState prevState = ctx.currentState;



  if (ctx.requestedState == ctx.currentState)
    return;

  Serial.print("[STATE] ");
  Serial.print(ctx.currentState);
  Serial.print(" → ");
  Serial.println(ctx.requestedState);

  switch (ctx.currentState) {

    case LOCKED:
      if (ctx.requestedState == UNLOCKING) {
        ctx.currentState = UNLOCKING;
      }
      break;

    case UNLOCKING:
      if (ctx.requestedState == RIDING) {
        ctx.currentState = RIDING;
      } else if (ctx.requestedState == LOCKED) {
        ctx.currentState = LOCKED;
      }
      break;

    case RIDING:
      if (ctx.requestedState == WARNING ||
          ctx.requestedState == EMERGENCY_STOP ||
          ctx.requestedState == LOCKED) {

        ctx.currentState = ctx.requestedState;
      }
      break;


    case WARNING:
      if (ctx.requestedState == RIDING ||
          ctx.requestedState == EMERGENCY_STOP) {
        ctx.currentState = ctx.requestedState;
      }
      break;

    case EMERGENCY_STOP:

      // Allow exit ONLY if latch is cleared
      if (!ctx.emergencyLatched) {

        if (ctx.requestedState == LOCKED) {
          ctx.currentState = LOCKED;
        }
        else if (ctx.requestedState == RIDING) {
          ctx.currentState = RIDING;
        }

      } else {
        // Still latched → stay here
        ctx.currentState = EMERGENCY_STOP;
      }

      break;

  }





  // Reset intent after processing
  ctx.requestedState = ctx.currentState;
}


void updateMotorPWM(const SystemContext &ctx) {
  uint8_t duty = map(ctx.motorThrottle, 0, 100, 0, 255);
  ledcWrite(MOTOR_PWM_PIN, duty);
}


extern SystemContext systemCtx;

class ScooterServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleClientConnected = true;
    Serial.println("[BLE] App connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    bleClientConnected = false;
    Serial.println("[BLE] App disconnected");


    systemCtx.lockRequested = true;   // auto-lock
    Serial.println("[BLE] App disconnected → locking scooter");

    delay(100);
    pServer->startAdvertising();
    Serial.println("[BLE] Advertising restarted");
  }
};


extern SystemContext systemCtx;


class CommandCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();

    value.trim();  // remove \n, spaces, etc.

    if (value.length() == 0) return;

    Serial.print("[BLE] Command received: ");
    Serial.println(value);

    if (value == "LOCK") {
      systemCtx.lockRequested = true;
    }
    else if (value == "UNLOCK") {
      systemCtx.unlockRequested = true;
    }
    else if (value == "RESET") {
      systemCtx.emergencyResetRequested = true;
      Serial.println("[BLE] Emergency reset requested");
    }


  }
};



class ZoneCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    value.trim();

    if (value.length() == 0) return;

    Serial.print("[BLE] Zone command: ");
    Serial.println(value);

    if (value == "CLEAR") {
      systemCtx.requestedZone = SystemContext::ZONE_CLEAR;
      systemCtx.zoneUpdateRequested = true;
    }
    else if (value == "SLOW") {
      systemCtx.requestedZone = SystemContext::ZONE_SLOW;
      systemCtx.zoneUpdateRequested = true;
    }
    else if (value == "RED") {
      systemCtx.requestedZone = SystemContext::ZONE_RED;
      systemCtx.zoneUpdateRequested = true;
    }
  }
};



void initBLE() {
  BLEDevice::init(SCOOTER_ID);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ScooterServerCallbacks());

  bleService = bleServer->createService(SCOOTER_SERVICE_UUID);

  commandChar = bleService->createCharacteristic(
    COMMAND_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  commandChar->setCallbacks(new CommandCallback());


  zoneChar = bleService->createCharacteristic(
    ZONE_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  zoneChar->setCallbacks(new ZoneCallback());

  telemetryChar = bleService->createCharacteristic(
    TELEMETRY_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  telemetryChar->addDescriptor(new BLE2902());



  bleService->start();

  bleServer->getAdvertising()->start();
  Serial.println("[BLE] Advertising started (BLEDevice)");
}

String buildTelemetryJSON(const SystemContext &ctx) {
  String s = "{";

  s += "\"state\":\"";
  switch (ctx.currentState) {
    case LOCKED: s += "LOCKED"; break;
    case UNLOCKING: s += "UNLOCKING"; break;
    case RIDING: s += "RIDING"; break;
    case WARNING: s += "WARNING"; break;
    case EMERGENCY_STOP: s += "EMERGENCY"; break;
  }
  s += "\",";

  s += "\"zone\":\"";
  switch (ctx.currentZone) {
    case SystemContext::ZONE_CLEAR: s += "CLEAR"; break;
    case SystemContext::ZONE_SLOW:  s += "SLOW"; break;
    case SystemContext::ZONE_RED:   s += "RED"; break;
  }
  s += "\",";

  s += "\"throttle\":" + String(ctx.motorThrottle) + ",";
  s += "\"battery\":" + String(ctx.batteryPercent);

  s += "}";

  return s;
}


void updateThrottleRamp(SystemContext &ctx) {

  uint8_t target  = ctx.allowedThrottle;
  uint8_t current = ctx.motorThrottle;

  if (current == target)
    return;

  uint8_t rampUp   = 0;
  uint8_t rampDown = 0;

  // -------- Select ramp profile based on state --------
  switch (ctx.currentState) {

    case LOCKED:
    case UNLOCKING:
      rampUp   = RAMP_UP_UNLOCKING;
      rampDown = RAMP_DOWN_RIDING;
      break;

    case RIDING:
      rampUp   = RAMP_UP_RIDING;
      rampDown = RAMP_DOWN_RIDING;
      break;

    case WARNING:
      rampUp   = RAMP_UP_WARNING;
      rampDown = RAMP_DOWN_WARNING;
      break;

    case EMERGENCY_STOP:
      rampUp   = 0;
      rampDown = RAMP_DOWN_EMERG;
      break;
  }

  // -------- Apply ramp --------
  if (current < target) {
    uint8_t delta = min<uint8_t>(rampUp, target - current);
    ctx.motorThrottle += delta;
  } else {
    uint8_t delta = min<uint8_t>(rampDown, current - target);
    ctx.motorThrottle -= delta;
  }
}



SystemContext systemCtx;

SensorsManager sensors;
SafetyController safety;
UIManager ui;

unsigned long lastLoopTime = 0;

void setup() {
  Serial.begin(115200);
  systemCtx.currentState = LOCKED;
  systemCtx.requestedState = LOCKED;


  sensors.begin();
  ui.begin();

  Serial.println("Smart Scooter System Booted");
  ledcAttach(
  MOTOR_PWM_PIN,
  MOTOR_PWM_FREQ,
  MOTOR_PWM_RESOLUTION
  );

  ledcWrite(MOTOR_PWM_CHANNEL, 0);  // motor off at boot

   // ---- BLE ----
  initBLE();   // 🔵 ADD THIS LINE
    
}

void loop() {
  unsigned long now = millis();
  
  // Simple scheduler tick (no blocking)
  if (now - lastLoopTime >= 20) {   // 50 Hz main loop
    lastLoopTime = now;

    sensors.update(systemCtx);      // 1️⃣ read sensors
    safety.evaluate(systemCtx);     // 2️⃣ enforce safety
    updateStateMachine(systemCtx);  // 3️⃣ state logic

    updateThrottleRamp(systemCtx);  // 🔵 STEP 2: smoothing layer

    updateMotorPWM(systemCtx);      // 4️⃣ motor output
    ui.update(systemCtx);           // 5️⃣ user feedback

    // ---- BLE Telemetry ----
    if (bleClientConnected &&
        now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {

      lastTelemetryMs = now;

      String payload = buildTelemetryJSON(systemCtx);
      telemetryChar->setValue(payload.c_str());
      telemetryChar->notify();
/*
      Serial.print("[TEL] ");
      Serial.println(payload);

*/
    }


  }
   // ---- Debug output (non-blocking) ----
  if (now - lastDebugPrint >= DEBUG_INTERVAL_MS) {
    lastDebugPrint = now;
    debugPrint(systemCtx);
  }
  // Watchdog feed will go here later
}
