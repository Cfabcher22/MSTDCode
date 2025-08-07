/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/

 ========== VARIABLE KEY ==========
  DOUT, CLK:     HX711 digital data and clock pins
  scale:         HX711 scale object
  calibrationMode: Toggle between calibration mode and normal operation
  knownWeight:   Known weight used during calibration (in pounds)
  calibrationFactor: Factor used to convert raw HX711 readings to pounds
==================================== */

#include "HX711.h"

#define DOUT 2  // HX711 Data pin
#define CLK  3  // HX711 Clock pin

HX711 scale;

// === USER SETTINGS ===
bool calibrationMode = false;         // Set to true to calibrate
float knownWeight = 15.1;             // lbs
float calibrationFactor = -20767.5;   // Replace with your factor

unsigned long startTime = 0;          // For tracking elapsed time

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Time (s),Weight (lbs)");  // CSV header for Arduino Plotter

  scale.begin(DOUT, CLK);
  delay(500);

  // Wait for HX711 to be ready
  while (!scale.is_ready()) {
    Serial.println("Waiting for HX711...");
    delay(500);
  }

  if (calibrationMode) {
    Serial.println("=== Calibration Mode ===");
    Serial.println("Taring... Remove all weight.");
    scale.set_scale();
    scale.tare();
    delay(1000);

    Serial.println("Place known weight on scale...");
    delay(5000);

    long raw = scale.get_units(10);  // Average of 10 readings
    float factor = raw / knownWeight;

    Serial.println("Raw average: " + String(raw));
    Serial.println("Calibration factor: " + String(factor, 3));
    Serial.println("Update 'calibrationFactor' and set 'calibrationMode = false'.");
  } else {
    scale.set_scale(calibrationFactor);
    scale.tare();
    delay(1000);
    startTime = millis();  // Start timing for plotting
  }
}

void loop() {
  if (!calibrationMode) {
    if (scale.is_ready()) {
      float weight = scale.get_units();
      weight = weight / 2.0;              // Correct doubled reading
      if (weight < 0) weight = 0.0;       // Clamp negatives

      float timeSeconds = (millis() - startTime) / 1000.0;

      // Output CSV for plotting: time, weight
      Serial.print(timeSeconds, 2);
      Serial.print(",");
      Serial.println(weight, 2);
    } else {
      Serial.println("0,0");  // Default if HX711 not ready
    }
    delay(100);  // Adjust plot smoothness
  } 
}
