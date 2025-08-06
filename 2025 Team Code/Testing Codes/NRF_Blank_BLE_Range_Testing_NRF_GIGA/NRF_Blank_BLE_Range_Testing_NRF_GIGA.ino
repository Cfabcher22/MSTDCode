/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/
*/
#include <ArduinoBLE.h>

// BLE Service & Characteristic UUIDs
BLEService testService("180C");
BLECharacteristic pingCharacteristic("2A56", BLENotify, 20);

// Timing
unsigned long previousMillis = 0;
const long interval = 500;

void setup() {
  // Do not call Serial.begin() when using battery-only mode

  if (!BLE.begin()) {
    // If BLE fails, go into deep sleep or blink an LED if available
    while (1);
  }

  BLE.setLocalName("XIAO-PING");
  BLE.setAdvertisedService(testService);
  testService.addCharacteristic(pingCharacteristic);
  BLE.addService(testService);
  BLE.advertise();
}

void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    unsigned long lastSent = millis();

    while (central.connected()) {
      unsigned long now = millis();

      if (now - lastSent >= interval) {
        lastSent = now;
        const char* msg = "PING";
        pingCharacteristic.writeValue((const uint8_t*)msg, strlen(msg));
      }

      BLE.poll();  // Must be called regularly
      delay(10);   // Light delay to reduce power use
    }

    // Restart advertising when disconnected
    BLE.advertise();
  }

  BLE.poll();
  delay(100);  // Idle polling interval
}
