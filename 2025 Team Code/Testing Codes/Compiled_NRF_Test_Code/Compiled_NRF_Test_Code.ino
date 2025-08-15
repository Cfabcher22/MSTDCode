/*
  XIAO nRF52840 + HX711 + ArduinoBLE — Pounds over BLE with reliable zero

  Advertised name: "NRF_FORCE_1"
  Service UUID:    180C
  Char UUID:       2A56
  Payload:         "ms_since_connect|pounds"

  What’s new:
    - Auto-tare ON CONNECT (once) → kills that 8 lb idle offset
    - Manual tare: hold BUTTON ≥ 600 ms anytime → quick triple green blink
    - Same pounds math you asked for (optional /2 correction, absolute value)
    - Battery LED only

  IMPORTANT: For auto-tare to be correct, connect with the platform UNLOADED.
*/

#include <ArduinoBLE.h>
#include "HX711.h"
#include <math.h>

// ========= USER CONFIG =========
static float calibrationFactor = -20767.5f; // Update after checking against a known weight
#define DEVICE_NAME            "NRF_FORCE_1"
#define SERVICE_UUID           "180C"
#define CHAR_UUID              "2A56"

#define DIVIDE_BY_TWO_FIX      1      // 1 = apply /2.0 correction (set 0 if not needed)
#define USE_ABSOLUTE_FORCE     1      // 1 = always-positive pounds
#define ZERO_SNAP_WINDOW_LB    0.05f  // small jitter snap-to-zero
#define TX_INTERVAL_MS         500
#define TARE_SAMPLES           25     // samples used for tare (more = steadier zero)

#define BUTTON_TARE_HOLD_MS    600    // hold time to trigger manual tare
// ===============================

// Pins
#define DOUT        2
#define CLK         3
#define BUTTON_PIN  4
#define BATTERY_PIN 0
#define RED_PIN     7
#define GREEN_PIN   D8
#define BLUE_PIN    D9

// Battery ADC (XIAO nRF52840)
const float ADC_REF_V   = 3.3f;
const float ADC_DIVIDER = 2.0f;      // 2:1 divider
const float ADC_MAX     = 4095.0f;   // 12-bit
const int   ADC_BITS    = 12;

// BLE
BLEService        forceService(SERVICE_UUID);
BLECharacteristic forceDataCharacteristic(CHAR_UUID, BLERead | BLENotify, 50);

// HX711
HX711 scale;

// State
unsigned long connectMillis = 0;
unsigned long previousTx    = 0;

bool didConnectTare   = false;   // run one time per connection
bool buttonWasDown    = false;
unsigned long buttonDownAt = 0;

static inline void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}
void blinkGreen(int count, int onMs=110, int offMs=90) {
  for (int i=0;i<count;i++){ setLED(false,true,false); delay(onMs); setLED(false,false,false); delay(offMs); }
}

float readBatteryVoltage() {
  uint16_t raw = analogRead(BATTERY_PIN);
  return (raw / ADC_MAX) * ADC_REF_V * ADC_DIVIDER;
}
int batteryPercent(float v) {
  if (v >= 4.2f) return 100;
  if (v <= 3.0f) return 0;
  return (int)((v - 3.0f) * 100.0f / (4.2f - 3.0f));
}
void showBatteryLED() {
  int pct = batteryPercent(readBatteryVoltage());
  if (pct > 75)      setLED(false, true,  false);
  else if (pct > 40) setLED(false, false, true);
  else               setLED(true,  false, false);
}

void doTare(int samples) {
  // brief visual cue: blue while taring
  setLED(false, false, true);
  scale.tare(samples);
  setLED(false, false, false);
}

float readPoundsRobust(float lastGoodLb) {
  float rawUnits;
  if (scale.wait_ready_timeout(100)) {
    rawUnits = scale.get_units(5);      // small average; uses calibrationFactor
  } else {
    return lastGoodLb;                  // hold last if not ready
  }
#if DIVIDE_BY_TWO_FIX
  float pounds = rawUnits * 0.5f;
#else
  float pounds = rawUnits;
#endif
#if USE_ABSOLUTE_FORCE
  pounds = fabs(pounds);
#endif
  if (fabs(pounds) < ZERO_SNAP_WINDOW_LB) pounds = 0.0f;
  return pounds;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(ADC_BITS);

  showBatteryLED();

  // HX711 warm-up and initial tare
  scale.begin(DOUT, CLK);
  delay(200);
  while (!scale.is_ready()) { delay(50); }
  scale.set_scale(calibrationFactor);
  delay(200);
  doTare(TARE_SAMPLES);       // initial zero
  blinkGreen(1);

  // BLE
  if (!BLE.begin()) {
    setLED(true, false, false);
    while (1) { delay(1000); }
  }
  BLE.setDeviceName(DEVICE_NAME);
  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(forceService);
  forceService.addCharacteristic(forceDataCharacteristic);
  BLE.addService(forceService);
  BLE.advertise();

  blinkGreen(1);
}

void loop() {
  showBatteryLED();
  BLE.poll();

  // Manual tare by button hold
  bool down = (digitalRead(BUTTON_PIN) == LOW);
  if (down && !buttonWasDown) { buttonDownAt = millis(); }
  if (down && (millis() - buttonDownAt >= BUTTON_TARE_HOLD_MS)) {
    doTare(TARE_SAMPLES + 10);
    blinkGreen(3);            // confirm manual tare
    // prevent repeated re-tare while still holding
    buttonDownAt = millis() + 100000;
  }
  buttonWasDown = down;

  BLEDevice central = BLE.central();
  if (!central) { delay(60); return; }

  // Connected
  connectMillis   = millis();
  previousTx      = 0;
  didConnectTare  = false;
  float lastGoodLb = 0.0f;

  while (central.connected()) {
    BLE.poll();
    showBatteryLED();

    // One-time auto-tare right after connect (make sure platform is empty!)
    if (!didConnectTare) {
      doTare(TARE_SAMPLES + 10);
      blinkGreen(2);
      didConnectTare = true;
    }

    // Manual tare while connected still works (same logic as above)
    bool d2 = (digitalRead(BUTTON_PIN) == LOW);
    if (d2 && !buttonWasDown) { buttonDownAt = millis(); }
    if (d2 && (millis() - buttonDownAt >= BUTTON_TARE_HOLD_MS)) {
      doTare(TARE_SAMPLES + 10);
      blinkGreen(3);
      buttonDownAt = millis() + 100000;
    }
    buttonWasDown = d2;

    unsigned long now = millis();
    float pounds = readPoundsRobust(lastGoodLb);
    lastGoodLb = pounds;

    if (now - previousTx >= TX_INTERVAL_MS) {
      previousTx = now;
      char msg[40];
      snprintf(msg, sizeof(msg), "%lu|%.1f", now - connectMillis, pounds);
      forceDataCharacteristic.writeValue((uint8_t*)msg, strlen(msg));
      // Serial.println(msg); // optional debug
    }
  }

  BLE.advertise();
}
