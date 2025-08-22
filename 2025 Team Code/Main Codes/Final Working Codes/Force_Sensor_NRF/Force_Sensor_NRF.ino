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
#define DOUT       2     // HX711 data pin
#define CLK        3     // HX711 clock pin
#define RED_LED    6    // Onboard RGB - Red channel
#define GREEN_LED  7    // Onboard RGB - Green channel
#define BLUE_LED   8    // Onboard RGB - Red channel
#define BUTTON_PIN 4     // Jumper wire input for test start

// === BLE SERVICE SETUP ===
BLEService forceService("180C");  // Custom BLE service UUID
BLECharacteristic forceDataCharacteristic("2A56", BLERead | BLENotify, 50);  // Transmit force data

// === HX711 INSTANCE ===
HX711 scale;

// === CONSTANTS ===
const float calibrationFactor = -20767.5;      // Convert raw scale readings to pounds
const long bleInterval = 500;                  // How often to send data (ms)
const unsigned long testDuration = 10000;      // Test session length in ms (10 seconds)

// === STATE VARIABLES ===
float baselineOffset = 0.0;
float lastValidReading = 0.0;
float peakForce = 0.0;
bool testActive = false;

unsigned long testStartTime = 0;
unsigned long previousMillis = 0;
unsigned long connectionMillis = 0;
unsigned long blinkTimer = 0;
bool blinkState = false;
bool isAdvertising = false;

// === LED CONTROL FUNCTION ===
// Turns on/off onboard RGB LED channels.
// Common-anode LED → LOW = ON, HIGH = OFF
void setLED(bool r, bool g) {
  digitalWrite(RED_LED, r ? LOW : HIGH);
  digitalWrite(GREEN_LED, g ? LOW : HIGH);
}

void setup() {
  // Setup serial monitor
  Serial.begin(115200);
  delay(1000);  // Wait for Serial to connect

  // Set LED and button pin modes
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Jumper input triggers LOW

  // Show red LED during startup
  setLED(true, false);

  // Initialize scale
  scale.begin(DOUT, CLK);
  delay(100);
  while (!scale.is_ready()) delay(100);  // Wait for scale to be ready
  scale.set_scale(calibrationFactor);
  scale.tare();  // Tare the scale to 0
  delay(1000);
  baselineOffset = scale.get_units(10);  // Store baseline

  // Initialize BLE
  if (!BLE.begin()) {
    setLED(true, false);  // Red = BLE startup failed
    while (1);
  }

  // BLE configuration
  BLE.setLocalName("Force Sensing Unit 1");
  BLE.setAdvertisedService(forceService);
  forceService.addCharacteristic(forceDataCharacteristic);
  BLE.addService(forceService);

  // Start advertising
  BLE.advertise();
  isAdvertising = true;
  blinkTimer = millis();

  Serial.println("BLE advertising started...");
}

void loop() {
  BLE.poll();  // Keep BLE stack alive
  BLEDevice central = BLE.central();  // Check if a central connects

  // === BLE CONNECTION HANDLING ===
  if (central) {
    Serial.println("Central connected");

    // Reset state variables
    connectionMillis = millis();
    previousMillis = 0;
    blinkTimer = millis();
    blinkState = false;
    isAdvertising = false;

    // Stay in loop while connected
    while (central.connected()) {
      unsigned long currentMillis = millis();

      // === Start test if jumper/button is pressed ===
      if (!testActive && digitalRead(BUTTON_PIN) == LOW) {
        testActive = true;
        testStartTime = currentMillis;
        peakForce = 0.0;
        Serial.println("Test started");
      }

      // === LED FEEDBACK ===
      if (testActive) {
        // Flash green during test
        if (currentMillis - blinkTimer >= 500) {
          blinkState = !blinkState;
          blinkTimer = currentMillis;
          setLED(false, blinkState);  // Flash green
        }
      } else {
        setLED(false, true);  // Solid green when connected but idle
      }

      // === BLE Data Transmission ===
      if (currentMillis - previousMillis >= bleInterval) {
        previousMillis = currentMillis;

        if (scale.is_ready()) {
          float pounds = abs(scale.get_units(5) - baselineOffset);
          lastValidReading = pounds;

          // Format data as "time|value"
          char message[40];
          snprintf(message, sizeof(message), "%lu|%.1f", currentMillis - connectionMillis, pounds);
          forceDataCharacteristic.writeValue((uint8_t*)message, strlen(message));

          // Check for peak force & test timeout
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

    // === BLE DISCONNECTED ===
    Serial.println("Disconnected from central");
    central.disconnect();
    BLE.advertise();  // Restart advertising
    isAdvertising = true;
    blinkTimer = millis();
    blinkState = false;
  }

  // === LED FEEDBACK WHEN DISCONNECTED ===
  if (!BLE.connected()) {
    unsigned long now = millis();

    if (isAdvertising) {
      if (now - blinkTimer >= 500) {
        blinkState = !blinkState;
        blinkTimer = now;
        setLED(blinkState, false);  // Flash red (simulating advertising)
      }
    } else {
      setLED(true, false);  // Solid red = not advertising, idle
    }
  }
}
