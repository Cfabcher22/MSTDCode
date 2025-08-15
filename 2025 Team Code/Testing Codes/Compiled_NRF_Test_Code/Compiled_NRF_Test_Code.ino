/*
  XIAO nRF52840 — NRF_FORCE_1 (BLE Peripheral)
  Sends "<ms>|<lbs>" every 500 ms AFTER a central connects.

  BLE:
    Service: 0x180C
    Char   : 0x2A56  (Notify + Read)

  HX711 pins (adjust if needed):
    DOUT=2, CLK=3
*/

#include <Arduino.h>
#include <ArduinoBLE.h>
#include "HX711.h"

// ===== HX711 =====
#define DOUT 2
#define CLK  3
HX711 scale;

// Adjust to your load cell calibration
const float CAL_FACTOR = -20767.5f;

// ===== BLE (180C / 2A56) =====
BLEService        forceService("180C");
BLECharacteristic forceChar("2A56", BLENotify | BLERead, 48);

// ===== Timing =====
const unsigned long SEND_INTERVAL_MS = 500;  // <-- requested rate
unsigned long t0 = 0;         // ms at connect
unsigned long lastSend = 0;   // last notification time
bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  // HX711 init
  scale.begin(DOUT, CLK);
  delay(200);
  Serial.println("NRF: waiting for HX711...");
  while (!scale.is_ready()) { delay(50); }
  scale.set_scale(CAL_FACTOR);
  scale.tare();
  delay(300);
  Serial.println("NRF: HX711 ready.");

  // BLE init
  if (!BLE.begin()) {
    Serial.println("NRF: BLE.begin() failed");
    while (1) {}
  }

  BLE.setLocalName("NRF_FORCE_1");
  BLE.setDeviceName("NRF_FORCE_1");
  BLE.setAdvertisedService(forceService);

  forceService.addCharacteristic(forceChar);
  BLE.addService(forceService);

  // Seed a value so readers have something immediately after subscribe
  forceChar.writeValue((const uint8_t*)"0|0.00", 6);

  BLE.advertise();
  Serial.println("NRF: advertising as NRF_FORCE_1");
}

void loop() {
  BLE.poll();

  bool isConnected = BLE.connected();

  // Edge: just connected
  if (isConnected && !wasConnected) {
    t0 = millis();
    lastSend = 0;  // force immediate send on next tick
    Serial.println("NRF: central connected");
  }

  // Edge: just disconnected
  if (!isConnected && wasConnected) {
    Serial.println("NRF: central disconnected (still advertising)");
  }

  wasConnected = isConnected;

  // Only stream when a central is connected
  if (isConnected) {
    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL_MS) {
      lastSend = now;

      // Read HX711 (non-blocking style)
      float pounds = 0.0f;
      if (scale.is_ready()) {
        long raw = scale.read();  // single conversion
        pounds = (raw - scale.get_offset()) / scale.get_scale();
      }

      // Optional small deadband around zero
      if (fabs(pounds) < 0.05f) pounds = 0.00f;

      char msg[40];
      int n = snprintf(msg, sizeof(msg), "%lu|%.2f", now - t0, pounds);
      if (n > 0) {
        // Notify subscribers
        forceChar.writeValue((const uint8_t*)msg, (size_t)n);
        // Debug (optional):
        // Serial.print("NRF OUT: "); Serial.println(msg);
      }
    }
  }

  delay(2);
}
