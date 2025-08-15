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

// === PIN DEFINITIONS ===
#define DOUT        2
#define CLK         3
#define BUTTON_PIN  4
#define BATTERY_PIN 0       
#define RED_PIN     7
#define GREEN_PIN   D8
#define BLUE_PIN    D9

// === BLE SERVICE SETUP ===
BLEService forceService("180C");
BLECharacteristic forceDataCharacteristic("2A56", BLERead | BLENotify, 50);

// === HX711 INSTANCE ===
HX711 scale;

// === CONSTANTS ===
const float calibrationFactor = -20767.5;
const long bleInterval = 500;
const unsigned long testDuration = 10000;

// === BATTERY MONITORING ===
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return raw * (5.0 / 1023.0) * 2.0;  // 2:1 voltage divider, 5V reference
}

int batteryPercent(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)(((voltage - 3.0) / (4.2 - 3.0)) * 100);
}

void showBatteryLED(int percent) {
  if (percent > 75) {
    setLED(false, true, false);   // Green
  } else if (percent > 40) {
    setLED(false, false, true);   // Blue
  } else {
    setLED(true, false, false);   // Red
  }
}

// === LED CONTROL ===
void setLED(bool redOn, bool greenOn, bool blueOn) {
  digitalWrite(RED_PIN, redOn ? HIGH : LOW);
  digitalWrite(GREEN_PIN, greenOn ? HIGH : LOW);
  digitalWrite(BLUE_PIN, blueOn ? HIGH : LOW);
}

// === STATE VARIABLES ===
float baselineOffset = 0.0;
float peakForce = 0.0;
bool testActive = false;

unsigned long testStartTime = 0;
unsigned long previousMillis = 0;
unsigned long connectionMillis = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BATTERY_PIN, INPUT);

  // Set initial LED color based on battery level
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
    while (1);
  }

  BLE.setLocalName("Force Sensing Unit 1");
  BLE.setAdvertisedService(forceService);
  forceService.addCharacteristic(forceDataCharacteristic);
  BLE.addService(forceService);

  BLE.advertise();
}

void loop() {
  // Continuously update battery LED
  float voltage = readBatteryVoltage();
  int percent = batteryPercent(voltage);
  showBatteryLED(percent);

  BLE.poll();
  BLEDevice central = BLE.central();

  if (central) {
    connectionMillis = millis();
    previousMillis = 0;

    while (central.connected()) {
      unsigned long currentMillis = millis();

      // Continuously update battery LED even while connected
      float v = readBatteryVoltage();
      int p = batteryPercent(v);
      showBatteryLED(p);

      if (!testActive && digitalRead(BUTTON_PIN) == LOW) {
        testActive = true;
        testStartTime = currentMillis;
        peakForce = 0.0;
      }

      if (currentMillis - previousMillis >= bleInterval) {
        previousMillis = currentMillis;

        if (scale.is_ready()) {
          float pounds = abs(scale.get_units(5) - baselineOffset);

          char message[40];
          snprintf(message, sizeof(message), "%lu|%.1f", currentMillis - connectionMillis, pounds);
          forceDataCharacteristic.writeValue((uint8_t*)message, strlen(message));

          if (testActive) {
            if (pounds > peakForce) peakForce = pounds;
            if ((currentMillis - testStartTime) >= testDuration) {
              testActive = false;
              Serial.print("Peak force: ");
              Serial.println(peakForce);
            }
          }
        }
      }
    }

    BLE.advertise();
  }

  delay(200);  // Smooth LED update
}
