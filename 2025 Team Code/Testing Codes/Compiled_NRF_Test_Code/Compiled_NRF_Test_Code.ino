/*
  Force Sensing Unit - BLE + HX711 + Plotting + LED Battery Indicator
  -------------------------------------------------------------------
  - Sends force data over BLE
  - Plots "force in pounds" to Arduino Serial Plotter
  - Indicates battery level using RGB LED
  - Optionally supports a soldered button to trigger test mode (commented out)

  Required board: Seeed XIAO nRF52840

  ========== VARIABLE & PIN KEY ==========
  DOUT, CLK:           HX711 data and clock pins (pins 2, 3)
  BUTTON_PIN:          Optional push button to trigger test (pin 4)
  BATTERY_PIN:         Reads battery voltage (pin 0)
  RED_PIN, GREEN_PIN, BLUE_PIN: RGB LED pins (pins 7, D8, D9)
  calibrationFactor:   Load cell calibration factor (set via previous testing)
  forceService:        BLE service for force data
  forceDataCharacteristic: BLE characteristic for live force values
  bleInterval:         Time between BLE updates (ms)
  testDuration:        Duration of force test (10s default)
  baselineOffset:      Zero-point tare reading at startup
  peakForce:           Highest force during test period
  testActive:          Whether a timed test is currently running
  testStartTime:       When the current test began
  previousMillis:      Last BLE transmission time
  connectionMillis:    BLE connection start time
==========================================
*/

#include <ArduinoBLE.h>
#include "HX711.h"

// === PIN DEFINITIONS ===
#define DOUT        2
#define CLK         3

// Uncomment the line below when button is soldered in
// #define BUTTON_PIN  4

#define BATTERY_PIN 0       
#define RED_PIN     7
#define GREEN_PIN   D8
#define BLUE_PIN    D9

// === BLE SERVICE SETUP ===
BLEService forceService("180C");
BLECharacteristic forceDataCharacteristic("2A56", BLERead | BLENotify, 50);

// === HX711 INSTANCE ===
HX711 scale;

// === CONFIGURABLE CONSTANTS ===
const float calibrationFactor = -20767.5;   // Replace with your calibration value
const long bleInterval = 500;               // BLE transmission every 500 ms
const unsigned long testDuration = 10000;   // Test duration in ms (10 seconds)

// === STATE VARIABLES ===
float baselineOffset = 0.0;
float peakForce = 0.0;
bool testActive = false;

unsigned long testStartTime = 0;
unsigned long previousMillis = 0;
unsigned long connectionMillis = 0;

// === Battery Reading ===
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return raw * (5.0 / 1023.0) * 2.0; // 2:1 voltage divider and 5V reference
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

// === LED Control ===
void setLED(bool redOn, bool greenOn, bool blueOn) {
  digitalWrite(RED_PIN, redOn ? HIGH : LOW);
  digitalWrite(GREEN_PIN, greenOn ? HIGH : LOW);
  digitalWrite(BLUE_PIN, blueOn ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize LED and button pins
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BATTERY_PIN, INPUT);

  // Uncomment after button is soldered in
  // pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Check battery level at startup
  float voltage = readBatteryVoltage();
  int percent = batteryPercent(voltage);
  showBatteryLED(percent);

  // Initialize HX711
  scale.begin(DOUT, CLK);
  delay(100);
  while (!scale.is_ready()) delay(100);
  scale.set_scale(calibrationFactor);
  scale.tare();                         // Set zero reference
  delay(1000);
  baselineOffset = scale.get_units(10); // Average to set baseline

  // Initialize BLE
  if (!BLE.begin()) {
    while (1); // Stay here if BLE fails
  }

  BLE.setLocalName("Force Sensing Unit 1");
  BLE.setAdvertisedService(forceService);
  forceService.addCharacteristic(forceDataCharacteristic);
  BLE.addService(forceService);
  BLE.advertise();
}

void loop() {
  // Update battery LED continuously
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

      // LED stays updated while connected
      float v = readBatteryVoltage();
      int p = batteryPercent(v);
      showBatteryLED(p);

      // === OPTIONAL BUTTON TEST START ===
      // Uncomment this block when push button is soldered to BUTTON_PIN
      /*
      if (!testActive && digitalRead(BUTTON_PIN) == LOW) {
        testActive = true;
        testStartTime = currentMillis;
        peakForce = 0.0;
      }
      */

      // === BLE + Serial Output ===
      if (currentMillis - previousMillis >= bleInterval) {
        previousMillis = currentMillis;

        if (scale.is_ready()) {
          float pounds = abs(scale.get_units(5) - baselineOffset);
          pounds = pounds / 2.0;          // Fix for doubled output
          if (pounds < 0) pounds = 0.0;   // Clamp negatives

          // === Send BLE force data ===
          char message[40];
          snprintf(message, sizeof(message), "%lu|%.1f", currentMillis - connectionMillis, pounds);
          forceDataCharacteristic.writeValue((uint8_t*)message, strlen(message));

          // === Serial Plotter output ===
          Serial.print("force in pounds:");
          Serial.println(pounds, 2);

          // === Track peak force during test ===
          if (testActive) {
            if (pounds > peakForce) peakForce = pounds;
            if ((currentMillis - testStartTime) >= testDuration) {
              testActive = false;
              Serial.print(\"Peak force: \");
              Serial.println(peakForce, 2);
            }
          }
        }
      }
    }

    // Restart advertising after disconnect
    BLE.advertise();
  }

  delay(200);  // Smooth LED update
}
