/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/
*/
#include "HX711.h"

// === HX711 PINS ===
#define DOUT 2
#define CLK  3

HX711 scale;

// === USER CONFIG ===
bool calibrationMode = false;     // Set to false after calibration is done
float knownWeight = 15.1;        // Weight in pounds used for calibration

// === Normal Mode Calibration Factor (Set this after calibration) ===
float calibrationFactor = -41054.305;  // Replace this after running calibration

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("HX711 Load Cell Test");

  scale.begin(DOUT, CLK);
  delay(500);

  while (!scale.is_ready()) {
    Serial.println("Waiting for HX711 to be ready...");
    delay(500);
  }

  if (calibrationMode) {
    Serial.println("=== Calibration Mode ===");
    Serial.println("Remove all weight. Taring...");
    scale.set_scale();  // Set to 1 temporarily
    scale.tare();       // Zero out the scale
    delay(1000);

    Serial.println("Now place your known weight (e.g., 15.1 lbs) on the sensor.");
    delay(5000); // Wait for user to place weight

    long reading = scale.get_units(10);  // Average 10 readings
    float calculatedCalibrationFactor = reading / knownWeight;

    Serial.println();
    Serial.print("Raw average reading: ");
    Serial.println(reading);
    Serial.print("Known weight: ");
    Serial.print(knownWeight);
    Serial.println(" lbs");
    Serial.print("Calculated calibration factor: ");
    Serial.println(calculatedCalibrationFactor, 3);
    Serial.println("\nCopy this number into the code for `calibrationFactor`.");
    Serial.println("Then set `calibrationMode = false` and re-upload.");
  } else {
    Serial.println("=== Normal Weighing Mode ===");
    scale.set_scale(calibrationFactor);
    scale.tare(); // Reset baseline
    delay(1000);
  }
}

void loop() {
  if (!calibrationMode) {
    if (scale.is_ready()) {
      float weight = scale.get_units();  // Returns pounds
      Serial.print("Weight: ");
      Serial.print(weight, 2);
      Serial.println(" lbs");
    } else {
      Serial.println("HX711 not ready.");
    }
    delay(250);
  }
}
