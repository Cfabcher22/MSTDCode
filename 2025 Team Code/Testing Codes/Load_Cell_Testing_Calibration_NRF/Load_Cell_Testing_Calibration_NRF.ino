/*
  ==========================================================================
  SKETCH 4: Baseline-Aware Dead Zone + Time/Weight Printing
  ==========================================================================
  - While |raw| ≤ DEAD_ZONE, show 0 and continuously track a baseline offset.
  - When |raw| > DEAD_ZONE, subtract the *tracked baseline* (not DEAD_ZONE).
    Example: baseline ≈ 3, raw = 18  => shown = 15 (correct true load).
  - Serial output (works with Serial Plotter):
      time_s:<val>\tweight_lbs:<val>
  ==========================================================================
*/
#include "HX711.h"

// === PIN DEFINITIONS ===
#define DOUT 2
#define CLK  3

// === USER SETTINGS ===
const float myCalibrationFactor = -41535;     // your factor
#define STABILIZATION_SECONDS 1               // warmup before tare

// Dead-zone *threshold* (when |raw| ≤ this, treat as baseline region)
const float DEAD_ZONE = 10.0f;

// How quickly the baseline follows near-zero drift (0.0..1.0)
// Larger = slower changes. 0.90 is very steady but still adapts.
const float BASELINE_ALPHA = 0.90f;

// Sampling interval (ms)
const unsigned long SAMPLE_INTERVAL_MS = 125; // ~8 Hz

// === GLOBALS ===
HX711 scale;
unsigned long lastSampleMs = 0;
bool measuring = false;

// Tracked baseline offset (what gets subtracted when above the zone)
float baselineOffset = 0.0f;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- Load Cell: Baseline-Aware Dead Zone ---");
  scale.begin(DOUT, CLK);
  scale.set_scale(myCalibrationFactor);

  Serial.print("Stabilizing for ");
  Serial.print(STABILIZATION_SECONDS);
  Serial.println(" seconds. Please wait...");
  for (int i = STABILIZATION_SECONDS; i > 0; --i) {
    delay(1000);
  }

  Serial.println("\nTare...");
  scale.tare(20);
  baselineOffset = 0.0f; // start fresh after tare

  Serial.println("Ready. Open Tools > Serial Plotter (115200).");
  Serial.println("Plot shows time_s and weight_lbs.\n");

  lastSampleMs = millis();
  measuring = true;
}

void loop() {
  if (!measuring) return;

  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  if (!scale.is_ready()) {
    static unsigned long lastWarn = 0;
    if (now - lastWarn > 1000UL) {
      Serial.println("HX711 not found.");
      lastWarn = now;
    }
    return;
  }

  // Single-sample read (no averaging)
  float raw = scale.get_units(1); // pounds (given your calibration)

  // Update baseline when we're inside the dead-zone band
  // The baseline tracks whatever small residual is present (e.g., +3 lb),
  // so we can subtract *that* once we leave the zone.
  if (fabs(raw) <= DEAD_ZONE) {
    baselineOffset = BASELINE_ALPHA * baselineOffset + (1.0f - BASELINE_ALPHA) * raw;
  }

  // Compute displayed weight
  float weight;
  if (fabs(raw) <= DEAD_ZONE) {
    // Inside zone: show 0
    weight = 0.0f;
  } else {
    // Outside zone: subtract the *tracked* baseline
    weight = raw - baselineOffset;

    // Optional: clamp tiny negatives to zero (noise)
    if (fabs(weight) < 0.05f) weight = 0.0f;
  }

  // Time in seconds
  float time_s = now / 1000.0f;

  // Print in tab-separated format for Serial Plotter
  Serial.print("time_s:");
  Serial.print(time_s, 2);
  Serial.print("\tweight_lbs:");
  Serial.println(weight, 2);
}
