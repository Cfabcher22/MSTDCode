#include <Arduino.h>

// ---------- USER SETTINGS ----------
const float R_TOP    = 10000.0f;  // battery+ -- R_TOP -- A0 -- R_BOTTOM -- GND
const float R_BOTTOM = 10000.0f;

const float ADC_REF_V     = 3.3f;   // nRF52840 ref
const int   ADC_MAX_COUNT = 4095;   // 12-bit

const float V_MIN = 3.0f;  // 0%
const float V_MAX = 4.2f;  // 100%

// Pins (XIAO nRF52840)
const int BATTERY_PIN = A0;
const int RED_PIN_HW  = 6;  // hardware pin wired to the "red" LED leg
const int GREEN_PIN_HW= 7;  // hardware pin wired to the "green" LED leg

// If your LED is common-anode to 3.3V, set ACTIVE_LOW = true.
// If your wires are crossed, set SWAP_RG = true.
const bool ACTIVE_LOW = false;  // false = common-cathode; true = common-anode
const bool SWAP_RG    = false;  // swap which pin drives which color

const unsigned long SAMPLE_MS = 200;

// ---------- HELPERS ----------
inline void drivePinPolarity(int pin, bool on, bool activeLow) {
  digitalWrite(pin, activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

void setRG(bool wantRed, bool wantGreen) {
  int redPin   = SWAP_RG ? GREEN_PIN_HW : RED_PIN_HW;
  int greenPin = SWAP_RG ? RED_PIN_HW   : GREEN_PIN_HW;

  drivePinPolarity(redPin,   wantRed,   ACTIVE_LOW);
  drivePinPolarity(greenPin, wantGreen, ACTIVE_LOW);
}

float readVbat() {
  int   raw  = analogRead(BATTERY_PIN);
  float vadc = (raw * ADC_REF_V) / (float)ADC_MAX_COUNT;
  const float DIV_GAIN = (R_TOP + R_BOTTOM) / R_BOTTOM; // 2.0 for 10k/10k
  return vadc * DIV_GAIN;
}

int pctFromV(float v) {
  if (v <= V_MIN) return 0;
  if (v >= V_MAX) return 100;
  return (int)((((v - V_MIN) / (V_MAX - V_MIN)) * 100.0f) + 0.5f);
}

// ---------- MAIN ----------
unsigned long lastMs = 0;

void setup() {
  delay(1500);                 // let USB enumerate
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(BATTERY_PIN, INPUT);
  pinMode(RED_PIN_HW,    OUTPUT);
  pinMode(GREEN_PIN_HW,  OUTPUT);
  setRG(false, false);

  // Print config once
  Serial.println("Battery monitor starting...");
  Serial.print("ACTIVE_LOW="); Serial.print(ACTIVE_LOW ? "true" : "false");
  Serial.print("  SWAP_RG=");  Serial.println(SWAP_RG ? "true" : "false");
}

void loop() {
  unsigned long now = millis();
  if (now - lastMs < SAMPLE_MS) return;
  lastMs = now;

  int   raw  = analogRead(BATTERY_PIN);
  float vadc = (raw * ADC_REF_V) / (float)ADC_MAX_COUNT;
  float vbat = readVbat();
  int   pct  = pctFromV(vbat);

  // Decide color
  const char* color;
  if (pct > 75) {
    setRG(false, true);    // GREEN
    color = "GREEN";
  } else if (pct >= 25) {
    setRG(true,  true);    // YELLOW (R+G)
    color = "YELLOW";
  } else {
    setRG(true,  false);   // RED
    color = "RED";
  }

  // Print debug line with expected color
  Serial.print("raw=");   Serial.print(raw);
  Serial.print("  Vadc="); Serial.print(vadc, 3);
  Serial.print("  Vbat="); Serial.print(vbat, 3);
  Serial.print("  %=");    Serial.print(pct);
  Serial.print("  color="); Serial.println(color);
}
