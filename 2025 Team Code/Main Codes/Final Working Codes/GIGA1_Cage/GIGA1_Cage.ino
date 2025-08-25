/*
  GIGA_FORWARDER — Motor + Baseplate Load Cell + BLE Bridge
  ---------------------------------------------------------
  SUMMARY
  - Reads a baseplate force from an HX711 load cell (myLoadCell1).
  - Drives a step/dir motor with simple timing in the main loop.
    * Commands arrive over BLE as text: "UP <sps>", "DOWN <sps>", "STOP".
    * <sps> = steps per second (optional). Enforced range: 50..3000 sps.
  - Forwards baseplate force over BLE every ~200 ms as: "BASE:<value>"
    (value is scaled pounds/force after calibration, multiplied by 2 per your code).
  - Acts as a BLE *peripheral* advertising service 0x190A with:
      - forwardCharacteristic (0x2BA1): notifies forwarded load value.
      - cmdCharacteristic     (0x2BA2): receives ASCII motor commands.

  VARIABLE / CONSTANT KEY
  - DOUT_PIN_LOAD_1, CLOCK_PIN_LOAD_1 : HX711 data/clock pins for baseplate sensor.
  - calibration_factor_body           : HX711 scale factor for baseplate.
  - myLoadCell1                       : HX711 object for baseplate load cell.

  - DIR_PIN, STEP_PIN                 : Motor direction/step pins.
  - motorRun (bool)                   : Motor enabled flag.
  - motorDir (bool)                   : Current direction (true=HIGH on DIR).
  - lastStepMicros (uint32_t)         : Time (micros) of last step toggle.
  - stepIntervalUs (uint32_t)         : Half-period for step pin toggling (μs).
  - stepLevel (bool)                  : Current logic level on STEP pin (toggles).

  - forwardService (0x190A)           : BLE service published by this device.
  - forwardCharacteristic (0x2BA1)    : BLE notify characteristic for "BASE:<val>".
  - cmdCharacteristic (0x2BA2)        : BLE write characteristic for commands.

  COMMAND FORMAT (via cmdCharacteristic):
    - "UP" [space] [sps]     → set direction up, optional speed, then run
    - "DOWN" [space] [sps]   → set direction down, optional speed, then run
    - "STOP"                 → stop motor
    Examples: "UP 1200", "DOWN 800", "STOP"

  NOTES
  - Stepping is done by toggling STEP each time stepIntervalUs elapses, so the
    effective step frequency is ~ (1 / (2 * stepIntervalUs)) per edge if your
    driver requires full pulses. Here we toggle the level each interval which
    yields one full pulse every two toggles. The chosen math gives the intended
    steps per second by setting stepIntervalUs = 1e6 / sps and toggling each time.
  - If your driver expects a minimum STEP high/low width, keep sps in range.
*/

#include <ArduinoBLE.h>   // BLE peripheral functionality
#include "HX711.h"        // Load cell ADC driver
#include <ctype.h>        // toupper() used in command parsing

// === HX711 Load Cell (Baseplate) ===
#define DOUT_PIN_LOAD_1 22                  // HX711 DOUT pin for baseplate
#define CLOCK_PIN_LOAD_1 23                 // HX711 SCK pin for baseplate
#define calibration_factor_body -9750.0     // Calibration factor (your tuned value)
HX711 myLoadCell1;                          // HX711 instance for baseplate

// === Motor pins ===
const int DIR_PIN  = 9;                     // Direction pin for step driver
const int STEP_PIN = 8;                     // Step pin for step driver

bool motorRun = false;                      // Whether motor stepping is active
bool motorDir = true;                       // Cached direction (true→HIGH on DIR)
unsigned long lastStepMicros = 0;           // Timestamp of last step toggle
unsigned long stepIntervalUs = 1000;        // Interval between toggles in μs (sets speed)
bool stepLevel = false;                     // Current STEP pin level

// === BLE Peripheral Role (to GIGA2) ===
// Custom service/characteristics (16-bit-like strings for readability)
BLEService forwardService("190A");                                              // Service for forwarding values + commands
BLECharacteristic forwardCharacteristic("2BA1", BLERead | BLENotify, 50);       // Notify baseplate force ("BASE:<val>")
BLECharacteristic cmdCharacteristic("2BA2", BLEWrite | BLEWriteWithoutResponse, 20); // Receive ASCII commands

// --- Motor helpers ---
// Set logical direction (maps to DIR pin HIGH/LOW)
static inline void setDirection(bool up) {
  motorDir = up;                                      // Cache requested direction
  digitalWrite(DIR_PIN, motorDir ? HIGH : LOW);       // Apply to hardware
}

// Set motor speed in steps per second, with basic clamping
static inline void setSpeedStepsPerSec(unsigned long sps) {
  if (sps < 50)   sps = 50;                           // Prevent too-slow timing
  if (sps > 3000) sps = 3000;                         // Limit to a safe ceiling
  stepIntervalUs = 1000000UL / sps;                   // Convert sps → μs per toggle
}

// Begin stepping if currently stopped
static inline void motorStartIfNeeded() {
  if (!motorRun) {
    motorRun = true;                                   // Enable stepping
    lastStepMicros = micros();                         // Reset timer baseline
  }
}

// Stop stepping and ensure STEP pin is low
static inline void motorStop() {
  motorRun = false;                                    // Disable stepping
  digitalWrite(STEP_PIN, LOW);                         // Idle STEP low
  stepLevel = false;                                   // Sync software state
}

// Parse ASCII command and act on motor
static void parseAndHandleCommand(const char* line) {
  char cmd[8] = {0};                                   // Holds "UP", "DOWN", "STOP"
  int i = 0;

  // Extract first token (command word) up to space
  while (*line && *line != ' ' && i < (int)sizeof(cmd)-1) {
    cmd[i++] = toupper(*line++);                       // Uppercase for case-insensitivity
  }
  cmd[i] = '\0';

  // Skip spaces between command and optional argument
  while (*line == ' ') line++;

  // Optional numeric argument: steps per second
  long sps = (*line) ? strtol(line, nullptr, 10) : 0;

  // Dispatch based on command
  if (strcmp(cmd, "UP") == 0) {
    setDirection(true);                                // DIR = HIGH
    if (sps > 0) setSpeedStepsPerSec(sps);             // Update speed if provided
    motorStartIfNeeded();                              // Start motor if not running
    Serial.println("CMD->UP");
  } else if (strcmp(cmd, "DOWN") == 0) {
    setDirection(false);                               // DIR = LOW
    if (sps > 0) setSpeedStepsPerSec(sps);             // Update speed if provided
    motorStartIfNeeded();                              // Start motor if not running
    Serial.println("CMD->DOWN");
  } else if (strcmp(cmd, "STOP") == 0) {
    motorStop();                                       // Stop stepping
    Serial.println("CMD->STOP");
  }
}

void setup() {
  Serial.begin(115200);                                // Debug/telemetry over USB

  // Prepare motor pins and default states
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(DIR_PIN, LOW);                          // Default direction
  digitalWrite(STEP_PIN, LOW);                         // STEP idle low

  // HX711 init for baseplate
  myLoadCell1.begin(DOUT_PIN_LOAD_1, CLOCK_PIN_LOAD_1);// Bind pins to HX711
  myLoadCell1.set_scale(calibration_factor_body);      // Apply calibration factor
  myLoadCell1.tare();                                  // Zero the scale
  Serial.println("Baseplate load cell ready!");

  // BLE init
  if (!BLE.begin()) {                                  // Start BLE stack
    Serial.println("BLE init failed!");
    while (1);                                         // Halt if BLE fails
  }

  BLE.setLocalName("GIGA_FORWARDER");                  // GAP name
  BLE.setAdvertisedService(forwardService);            // Advertise our service

  // Add characteristics to service, then add service to peripheral
  forwardService.addCharacteristic(forwardCharacteristic);
  forwardService.addCharacteristic(cmdCharacteristic);
  BLE.addService(forwardService);

  BLE.advertise();                                     // Start advertising
  Serial.println("GIGA1 advertising as 'GIGA_FORWARDER'");
}

void loop() {
  BLE.poll();                                          // Handle BLE events (connections/writes)

  // --- Motor stepping ---
  if (motorRun) {
    unsigned long now = micros();                      // Current microsecond timer
    // Time to toggle STEP pin?
    if ((unsigned long)(now - lastStepMicros) >= stepIntervalUs) {
      lastStepMicros = now;                            // Move target window
      stepLevel = !stepLevel;                          // Toggle level
      digitalWrite(STEP_PIN, stepLevel ? HIGH : LOW);  // Emit edge to driver
    }
  }

  // --- Send load cell value every 200 ms ---
  static unsigned long lastSendMs = 0;                 // Last telemetry send time
  unsigned long nowMs = millis();                      // Current ms tick
  if (nowMs - lastSendMs >= 200) {                     // 5 Hz send rate
    lastSendMs = nowMs;

    // Read scaled units from HX711; multiply by 2 as in your original logic
    float baseForce = myLoadCell1.get_units() * 2;

    // Build "BASE:<value>" payload with 2 decimals
    char msg[32];
    snprintf(msg, sizeof(msg), "BASE:%.2f", baseForce);

    // Notify subscribers and print to serial
    forwardCharacteristic.writeValue((uint8_t*)msg, strlen(msg));
    Serial.print("Forwarded: ");
    Serial.println(msg);
  }

  // --- Motor command from GIGA2 (BLE write to cmdCharacteristic) ---
  if (cmdCharacteristic.written()) {                   // Check if a write occurred
    char cmdBuf[21] = {0};                             // Max 20 chars (+1 for NUL)
    int len = cmdCharacteristic.valueLength();         // How many bytes were written
    len = min(len, 20);                                // Clamp to our buffer
    cmdCharacteristic.readValue((uint8_t*)cmdBuf, len);// Copy bytes into buffer
    cmdBuf[len] = '\0';                                // Ensure C-string terminator

    Serial.print("Received cmd: ");
    Serial.println(cmdBuf);

    parseAndHandleCommand(cmdBuf);                     // Execute command
  }
}
