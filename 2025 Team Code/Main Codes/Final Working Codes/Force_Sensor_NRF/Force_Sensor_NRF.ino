/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Required board to be used: 
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/

  ============================================================================
  XIAO nRF52840 Force Sensor — Battery-Only, HX711, BLE to GIGA (Documented)
  ============================================================================

  SUMMARY
  -------
  - Reads a load cell via HX711 and converts to pounds.
  - Sends ONLY the force (as text like "12.3") over BLE:
      * Service UUID: 0x180C
      * Characteristic UUID: 0x2A56 (BLERead | BLENotify)
  - Battery indicator on both onboard + external LEDs for the first 3 seconds:
      * ≤10%  → blink RED
      * <30%  → solid RED
      * 30–59%→ YELLOW (RED+GREEN)
      * ≥60%  → solid GREEN
  - After the 3s window:
      * If battery ≤10% → blink RED
      * Else BLUE shows BLE status: blink = advertising, solid = connected
  - External RGB LED is common-anode (active-LOW) on pins 7 8 9.

  VARIABLE / CONSTANT KEY
  -----------------------
  PINS:
    ONB_RED_PIN / ONB_GREEN_PIN / ONB_BLUE_PIN  -> Onboard RGB (active-LOW)
    EXT_RED_PIN (6) / EXT_GREEN_PIN (7) / EXT_BLUE_PIN (8) -> External RGB (active-LOW)
    DOUT (2) / CLK (3)                           -> HX711 data/clock
    VBAT_PIN (A0)                                -> Battery divider input

  BLE:
    forceService ("180C")                        -> Primary service UUID
    forceDataCharacteristic ("2A56")             -> Notifiable, text payload of pounds

  FORCE:
    calibrationFactor                            -> HX711 raw-to-lbs scale factor
    DEAD_ZONE                                    -> |lbs| treated as zero (baseline follows) due to constant upward value creeping
                                                    Requires the minimum force applied greater than 10 pounds for value to be displayed
    BASELINE_ALPHA                               -> Baseline follow rate (0..1, higher = slower follow)
    baselineOffset / lastValidReading / peakForce-> State for smoothing and reporting

  BATTERY MODEL:
    ADC_REF_VOLT (3.30 V)                        -> ADC reference voltage
    ADC_BITS (12) / ADC_COUNTS                   -> ADC resolution
    VBAT_DIVIDER_RATIO (2.0)                     -> Divider from cell to ADC pin
    VBAT_EMPTY_PER_CELL (3.30 V)                 -> Empty threshold per cell
    VBAT_FULL_PER_CELL  (4.20 V)                 -> Full threshold per cell
    LED_RED_MAX_PCT (30), LED_YELLOW_MAX_PCT (60), LED_RED_BLINK_PCT (10)

  TIMING:
    bleInterval (100 ms)                         -> 10 Hz BLE updates
    startupWindowMs (3000 ms)                    -> Battery LED window at boot
    tickBlue / tickRed                           -> 500 ms blink tickers for status LEDs
*/

#include <ArduinoBLE.h>          // BLE API
#include "HX711.h"               // HX711 load cell amplifier
#include <math.h>                // fabsf, isfinite
#include <string.h>              // strlen, snprintf

// ===== LED PINS =====
// Onboard RGB (active-LOW: write LOW to turn LED on)
#define ONB_RED_PIN    12            // Onboard red LED pin
#define ONB_GREEN_PIN  13            // Onboard green LED pin
#define ONB_BLUE_PIN   14            // Onboard blue LED pin

// External RGB (common-anode, so also active-LOW)
#define EXT_RED_PIN     7            // External red
#define EXT_GREEN_PIN   8            // External green
#define EXT_BLUE_PIN    9            // External blue

// ===== HX711 (your wiring) =====
#define DOUT 2                   // HX711 data output → XIAO pin 2
#define CLK  3                   // HX711 clock       → XIAO pin 3
HX711 scale;                     // HX711 instance
const float calibrationFactor = -41535.0f;// Calfactor was determined by previous teams. Uncertain how they obtained it

// Force shaping (display logic)
const float DEAD_ZONE      = 10.0f;  // |delta| ≤ 10 lb → report 0 and let baseline follow
const float BASELINE_ALPHA = 0.90f;  // Baseline follow rate (closer to 1 = slower follow)

// Force state
float baselineOffset   = 0.0f;       // Running zero baseline in lbs
float lastValidReading = 0.0f;       // Last published lbs
float peakForce        = 0.0f;       // Peak lbs since boot (kept for possible future use)

// ===== BATTERY SENSE =====
#define VBAT_PIN            A0       // Voltage divider input (cell/2 to ADC)
#define ADC_REF_VOLT        3.30f    // ADC reference voltage
#define ADC_BITS            12       // 12-bit ADC
#define ADC_COUNTS          ((1UL << ADC_BITS) - 1) // 4095 for 12-bit
#define VBAT_DIVIDER_RATIO  2.00f    // Divider factor (ADC sees half the cell voltage)
#define VBAT_SAMPLES        8        // Number of ADC samples for averaging

// Battery thresholds
#define CELL_COUNT              1     // 1S Li-ion
#define VBAT_EMPTY_PER_CELL     3.30f // "Empty" voltage per cell
#define VBAT_FULL_PER_CELL      4.20f // "Full" voltage per cell
#define LED_RED_MAX_PCT         30    // <30% → red in startup window
#define LED_YELLOW_MAX_PCT      60    // 30–59% → yellow in startup window
#define LED_RED_BLINK_PCT       10    // ≤10% → blink red (during/after startup)
const float VBAT_EMPTY = VBAT_EMPTY_PER_CELL * CELL_COUNT; // Absolute empty volts
const float VBAT_FULL  = VBAT_FULL_PER_CELL  * CELL_COUNT; // Absolute full volts

// ===== BLE =====
BLEService        forceService("180C");                          // Define BLE service UUID
BLECharacteristic forceDataCharacteristic("2A56",                // Define characteristic UUID
                                          BLERead | BLENotify,   // Readable, notifiable
                                          12);                   // Up to 11 chars + null for "xxx.x"

// ===== TIMING =====
const unsigned long bleInterval     = 100;  // Send BLE updates every 100 ms (10 Hz)
const unsigned long startupWindowMs = 3000; // Battery LED window duration

unsigned long previousMillis  = 0;          // Time of last BLE send
unsigned long startupMs       = 0;          // Boot time reference
unsigned long tickBlueMs      = 0;          // Last toggle time for blue blink
bool          tickBlue        = false;      // Blue blink state
unsigned long tickRedMs       = 0;          // Last toggle time for red blink
bool          tickRed         = false;      // Red blink state

// ===== LED HELPERS (explicitly drive all channels each call) =====

// Drive onboard RGB (active-LOW)
static inline void setOnboard(bool r, bool g, bool b) {
  digitalWrite(ONB_RED_PIN,   r ? LOW : HIGH);  // On if r==true
  digitalWrite(ONB_GREEN_PIN, g ? LOW : HIGH);  // On if g==true
  digitalWrite(ONB_BLUE_PIN,  b ? LOW : HIGH);  // On if b==true
}

// Drive external RGB (active-LOW)
static inline void setExternal(bool r, bool g, bool b) {
  digitalWrite(EXT_RED_PIN,   r ? LOW : HIGH);  // On if r==true
  digitalWrite(EXT_GREEN_PIN, g ? LOW : HIGH);  // On if g==true
  digitalWrite(EXT_BLUE_PIN,  b ? LOW : HIGH);  // On if b==true
}

// Drive both onboard + external together
static inline void setBoth(bool r, bool g, bool b) {
  setOnboard(r, g, b);                          // Set onboard LED
  setExternal(r, g, b);                         // Set external LED
}

// ===== BATTERY READ =====
// Read VBAT with simple averaging and convert to volts + percent (0–100)
void readBattery(float &vbat, int &pct) {
  unsigned long acc = 0;                        // Accumulator for ADC samples
  for (int i = 0; i < VBAT_SAMPLES; ++i) {
    acc += analogRead(VBAT_PIN);                // Read ADC
    delayMicroseconds(300);                     // Tiny pause for stability
  }
  const float adc   = (float)acc / VBAT_SAMPLES;              // Average ADC counts
  const float v_adc = (adc / ADC_COUNTS) * ADC_REF_VOLT;      // ADC voltage at pin
  vbat = v_adc * VBAT_DIVIDER_RATIO;                          // Back-calculate cell voltage

  float p = (vbat - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY);   // Linear percent
  if (p < 0) p = 0; if (p > 1) p = 1;                         // Clamp to [0,1]
  pct = (int)lroundf(p * 100.0f);                             // Convert to integer percent
}

// ===== HX711 INIT =====
// Initialize the HX711, tare, and capture a baseline
void initScale() {
  pinMode(DOUT, INPUT);                       // HX711 data line is input
  pinMode(CLK,  OUTPUT);                      // HX711 clock is output
  digitalWrite(CLK, LOW);                     // Keep clock low at idle (recommended)
  scale.begin(DOUT, CLK);                     // Start HX711 on the chosen pins
  delay(100);                                 // Brief settle

  scale.set_scale(calibrationFactor);         // Apply calibration factor (raw → lbs)
  scale.tare();                               // Zero the scale
  delay(150);                                 // Let tare settle

  baselineOffset = scale.get_units(10);       // Average a few samples to seed baseline
}

// ===== Arduino SETUP =====
void setup() {
  // LED pins to outputs and turn everything off
  pinMode(ONB_RED_PIN, OUTPUT);
  pinMode(ONB_GREEN_PIN, OUTPUT);
  pinMode(ONB_BLUE_PIN, OUTPUT);
  pinMode(EXT_RED_PIN,   OUTPUT);
  pinMode(EXT_GREEN_PIN, OUTPUT);
  pinMode(EXT_BLUE_PIN,  OUTPUT);
  setBoth(false, false, false);               // All LEDs off

  // Quick proof animation: R -> G -> B (both onboard + external)
  setBoth(true,  false, false);  delay(250);  // Red
  setBoth(false, true,  false);  delay(250);  // Green
  setBoth(false, false, true );  delay(250);  // Blue
  setBoth(false, false, false);               // Off

  analogReadResolution(ADC_BITS);             // Use 12-bit ADC reads

  initScale();                                // Initialize HX711 (tare + baseline)

  // Start BLE and expose our service/characteristic
  if (!BLE.begin()) {                         // If BLE fails to start
    while (1) {                               // Blink red forever to signal failure
      setBoth(true, false, false);  delay(220);
      setBoth(false, false, false); delay(220);
    }
  }
  BLE.setLocalName("Force Sensing Unit 1");   // Device name that the GIGA will see
  BLE.setAdvertisedService(forceService);     // Advertise our service
  forceService.addCharacteristic(forceDataCharacteristic); // Attach characteristic to service
  BLE.addService(forceService);               // Add service to peripheral
  BLE.advertise();                            // Begin advertising

  // Initialize timers/tickers
  startupMs  = millis();                      // Mark boot time
  tickBlueMs = tickRedMs = startupMs;         // Start blink tickers now
}

// ===== Arduino LOOP =====
void loop() {
  BLE.poll();                                 // Service BLE stack events
  const unsigned long now = millis();         // Current time (ms)

  // Update blink tickers every 500 ms
  if (now - tickBlueMs >= 500) { tickBlueMs = now; tickBlue = !tickBlue; }
  if (now - tickRedMs  >= 500) { tickRedMs  = now; tickRed  = !tickRed;  }

  // --- LED policy (battery window then status) ---
  float vbat = 0.0f; int battPct = 0;         // Holders for voltage and percent
  readBattery(vbat, battPct);                  // Sample battery
  const bool dying     = (battPct <= LED_RED_BLINK_PCT); // Critical battery?
  const bool connected = BLE.connected();      // BLE link state

  if (now - startupMs < startupWindowMs) {     // During first 3 seconds after boot
    if (dying)                                 // ≤10% → blink RED
      setBoth(tickRed, false, false);
    else if (battPct < LED_RED_MAX_PCT)        // <30% → solid RED
      setBoth(true, false, false);
    else if (battPct < LED_YELLOW_MAX_PCT)     // 30–59% → YELLOW
      setBoth(true, true, false);
    else                                       // ≥60% → GREEN
      setBoth(false, true, false);
  } else {                                     // After the battery window
    if (dying) {                               // Critical battery → blink RED
      setBoth(tickRed, false, false);
    } else {                                   // Otherwise, blue = BLE status
      setBoth(false, false, connected ? true : tickBlue); // Solid if connected; blink if advertising
    }
  }

  // --- BLE payload at 10 Hz (send ONLY when connected) ---
  if (connected && (now - previousMillis >= bleInterval)) {
    previousMillis = now;                      // Mark send time

    float displayLbs = lastValidReading;       // Default to last value if no fresh sample

    // Take a quick, non-blocking HX711 sample if one is ready
    if (scale.wait_ready_timeout(1)) {         // Don’t stall; just grab if ready now
      float raw   = scale.get_units(1);        // Read 1-sample average (already scaled to lbs)
      float delta = raw - baselineOffset;      // Difference from running baseline

      if (fabsf(delta) <= DEAD_ZONE) {         // Inside dead zone?
        // Report 0 and let baseline creep toward raw (to track slow drift)
        baselineOffset = BASELINE_ALPHA * baselineOffset
                       + (1.0f - BASELINE_ALPHA) * raw;
        displayLbs = 0.0f;
      } else {
        // Outside dead zone → report the delta
        displayLbs = delta;
        if (!isfinite(displayLbs) || displayLbs < 0.0f) displayLbs = 0.0f; // Guard rails
        if (fabsf(displayLbs) < 0.02f) displayLbs = 0.0f;                  // Tiny noise clamp
      }

      lastValidReading = displayLbs;           // Remember for next loop
      if (displayLbs > peakForce) peakForce = displayLbs; // Track peak (not transmitted)
    }

    // Format BLE payload as ASCII text (e.g., "12.3")
    char payload[12];                           // Enough for up to "9999.9\0"
    snprintf(payload, sizeof(payload), "%.1f", lastValidReading);
    forceDataCharacteristic.writeValue((uint8_t*)payload, strlen(payload)); // Notify subscribers
  }
}
