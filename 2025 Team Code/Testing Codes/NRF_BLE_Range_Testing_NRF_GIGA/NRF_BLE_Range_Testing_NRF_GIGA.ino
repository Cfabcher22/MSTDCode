/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/
*/

#include <ArduinoBLE.h>
#include "HX711.h"

// === Pins ===
#define DOUT        2
#define CLK         3
#define BUTTON_PIN  4
#define BATTERY_PIN 0       
#define RED_PIN     7
#define GREEN_PIN   D8
#define BLUE_PIN    D9

// === BLE ===
BLEService forceService("180C");
BLECharacteristic forceDataCharacteristic("2A56", BLERead | BLENotify, 50);

// === Load Cell ===
HX711 scale;
const float calibrationFactor = -20767.5;
const long bleInterval = 100;  // FAST DATA

// === Battery Monitor ===
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return raw * (5.0 / 1023.0) * 2.0;
}

int batteryPercent(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)(((voltage - 3.0) / (4.2 - 3.0)) * 100);
}

void showBatteryLED(int percent) {
  if (percent > 75) setLED(false, true, false);     // Green
  else if (percent > 40) setLED(false, false, true);// Blue
  else setLED(true, false, false);                  // Red
}

void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN, r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN, b ? HIGH : LOW);
}

// === State ===
float baselineOffset = 0.0;
float peakForce = 0.0;
bool testActive = false;

unsigned long testStartTime = 0;
unsigned long previousMillis = 0;
unsigned long connectionMillis = 0;
const unsigned long testDuration = 10000;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BATTERY_PIN, INPUT);

  float voltage = readBatteryVoltage();
  int percent = batteryPercent(voltage);
  showBatteryLED(percent);

  scale.begin(DOUT, CLK);
  delay(100);
  while (!scale.is_ready()) delay(100);
  scale.set_scale(calibrationFactor);
  scale.tare();
  delay(1000);
  baselineOffset = scale.get_units(10);

  if (!BLE.begin()) {
    Serial.println("BLE init failed");
    while (1);
  }

  BLE.setLocalName("Force Sensing Unit 1");
  BLE.setAdvertisedService(forceService);
  forceService.addCharacteristic(forceDataCharacteristic);
  BLE.addService(forceService);

  BLE.advertise();
}

void loop() {
  float voltage = readBatteryVoltage();
  int percent = batteryPercent(voltage);
  showBatteryLED(percent);

  BLE.poll();
  BLEDevice central = BLE.central();

  if (central) {
    Serial.println("Connected to GIGA");
    connectionMillis = millis();
    previousMillis = 0;

    while (central.connected()) {
      unsigned long now = millis();
      float v = readBatteryVoltage();
      int p = batteryPercent(v);
      showBatteryLED(p);

      if (!testActive && digitalRead(BUTTON_PIN) == LOW) {
        testActive = true;
        testStartTime = now;
        peakForce = 0.0;
      }

      if (now - previousMillis >= bleInterval) {
        previousMillis = now;

        if (scale.is_ready()) {
          float pounds = abs(scale.get_units(5) - baselineOffset);
          char message[40];
          snprintf(message, sizeof(message), "%lu|%.1f", now - connectionMillis, pounds);
          forceDataCharacteristic.writeValue((uint8_t*)message, strlen(message));

          if (testActive) {
            if (pounds > peakForce) peakForce = pounds;
            if ((now - testStartTime) >= testDuration) {
              testActive = false;
              Serial.print("Peak force: ");
              Serial.println(peakForce);
            }
          }
        }
      }
    }

    Serial.println("Disconnected from GIGA");
    BLE.advertise();  // Auto reconnect
  }

  delay(100);
}
