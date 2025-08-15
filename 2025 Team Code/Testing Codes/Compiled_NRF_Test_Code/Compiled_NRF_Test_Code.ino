/*
  XIAO nRF52840 — NRF_FORCE_1 (BLE Peripheral)
  - Service: 0x180C (custom usage)
  - Char   : 0x2A56 (Notify) — payload "<ms>|<lbs>"
  - HX711 on DOUT=2, CLK=3
*/

#include <Arduino.h>
#include <ArduinoBLE.h>
#include "HX711.h"

// ---- HX711 pins (adjust if needed) ----
#define DOUT 2
#define CLK  3

HX711 scale;

// ---- Calibration (adjust to your load cell) ----
const float CAL_FACTOR = -20767.5f;  // matches your prior notes

// ---- BLE (standard 180C with 2A56 char used as custom notify) ----
BLEService forceService("180C");
BLECharacteristic forceChar("2A56", BLENotify | BLERead, 64);  // "<ms>|<lbs>"

unsigned long t0 = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  // HX711 init
  scale.begin(DOUT, CLK);
  delay(200);
  Serial.println("NRF: waiting for HX711...");
  while (!scale.is_ready()) {
    delay(100);
  }
  scale.set_scale(CAL_FACTOR);
  scale.tare();
  delay(500);
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

  // seed
  forceChar.writeValue((const uint8_t*)"0|0.0", 5);

  BLE.advertise();
  Serial.println("NRF: advertising as NRF_FORCE_1");
  t0 = millis();
}

void loop() {
  BLE.poll();

  // Read one sample; avoid long averaging to keep latency low
  long raw = scale.read();  // single read
  float pounds = (raw - scale.get_offset()) / scale.get_scale();

  // Clamp noise near zero if you want (optional)
  if (fabs(pounds) < 0.05f) pounds = 0.0f;

  // Every ~100 ms, notify "<ms>|<lbs>"
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last >= 100) {
    last = now;
    char msg[48];
    int n = snprintf(msg, sizeof(msg), "%lu|%.2f", now - t0, pounds);
    if (n > 0) {
      forceChar.writeValue((const uint8_t*)msg, (size_t)n);
      // Debug:
      // Serial.print("NRF OUT: "); Serial.println(msg);
    }
  }

  delay(2);
}
