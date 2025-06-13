/*
  Board Selection Required:
  -------------------------
  In Arduino IDE, go to:
  Tools → Board → Seeed nRF52 Boards → "XIAO nRF52840 (No Updates)"
  Seeed Stuido XIAO nRF52840 board package and instructions added from
  the following URL: https://wiki.seeedstudio.com/XIAO_BLE/

  ===============================
  Variable Descriptions
  ===============================
  HX711 scale                → Object for interfacing with the load cell via HX711
  calibrationFactor          → Conversion factor for raw HX711 data to pounds
  bleInterval                → Time interval (ms) between BLE characteristic updates
  testDuration               → Duration (ms) of each test session (default: 10 seconds)
  baselineOffset             → Baseline force value captured during tare operation
  lastValidReading           → Most recent filtered and offset-corrected force value
  peakForce                  → Maximum force measured during the current test session
  testActive                 → Boolean flag indicating whether a test session is active
  testStartTime              → Timestamp (ms) when the test session started
  previousMillis             → Timestamp used to manage BLE update intervals
  connectionMillis           → Timestamp when BLE central connected
*/

#include <ArduinoBLE.h>   // Library for BLE functionality
#include "HX711.h"        // Library for reading from HX711 load cell amplifier

#define DOUT 2            // Data pin for HX711
#define CLK 3             // Clock pin for HX711
HX711 scale;              // Create scale object

// Define BLE service and characteristic
BLEService forceService("180C");  // Custom service UUID
BLECharacteristic forceDataCharacteristic("2A56", BLERead | BLENotify, 50);  // Characteristic for force data

// Calibration and timing constants
const float calibrationFactor = -20767.5;           // Calibration factor to convert raw data to pounds
const long bleInterval = 500;                       // Interval between BLE notifications (ms)
const unsigned long testDuration = 10000;           // Duration of each test session (ms)

// Runtime variables
float baselineOffset = 0.0;                         // Offset value from tare for force normalization
float lastValidReading = 0.0;                       // Most recent valid force reading
float peakForce = 0.0;                              // Peak force during the current test
bool testActive = false;                            // Flag to track if a test is in progress

unsigned long testStartTime = 0;                    // Start time of the current test session
unsigned long previousMillis = 0;                   // Last time a BLE update was sent
unsigned long connectionMillis = 0;                 // Time when BLE central connected

void setup() {
  Serial.begin(115200);       // Initialize serial communication at 115200 baud
  while (!Serial);            // Wait for Serial to connect (only needed during development with USB)

  scale.begin(DOUT, CLK);     // Initialize HX711 with specified data and clock pins
  Serial.println("Initializing HX711...");
  while (!scale.is_ready()) { // Wait until the HX711 is ready to read
    Serial.println("Waiting for HX711...");
    delay(100);
  }

  scale.set_scale(calibrationFactor); // Apply calibration factor to the scale
  Serial.println("Taring scale...");
  scale.tare();                       // Set tare (zero out current load)
  delay(1000);                        // Give some time to settle

  baselineOffset = scale.get_units(10); // Take 10 averaged readings as baseline offset
  Serial.print("Software tare baseline: ");
  Serial.println(baselineOffset, 4);    // Print the offset value

  // Initialize BLE module
  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    while (1); // Halt the program if BLE fails to initialize
  }

  // Configure BLE settings
  BLE.setLocalName("Force Sensing Unit 1");         // Device name
  BLE.setAdvertisedService(forceService);    // Attach service to advertisement
  forceService.addCharacteristic(forceDataCharacteristic); // Add characteristic to service
  BLE.addService(forceService);              // Add the service to the BLE stack
  BLE.advertise();                           // Start advertising

  Serial.println("BLE advertising started...");
  Serial.println("Press 'S' in Serial Monitor to start a 10-second test.");
}

void loop() {
  BLEDevice central = BLE.central();  // Check if a central device is connected

  // Check for 'S' key press to start test
  if (Serial.available()) {
    char input = Serial.read();      // Read input from Serial
    if ((input == 'S' || input == 's') && !testActive) {
      Serial.println("Starting 10-second test session...");
      peakForce = 0.0;               // Reset peak force
      testStartTime = millis();      // Record start time
      testActive = true;             // Activate test
    }
  }

  // If a central device is connected
  if (central) {
    if (connectionMillis == 0) {
      // First time connecting
      Serial.print("Connected to central: ");
      Serial.println(central.address());
      connectionMillis = millis();  // Record time of connection
      previousMillis = 0;           // Reset BLE update timer
    }

    // While still connected
    while (central.connected()) {
      unsigned long currentMillis = millis();

      // Check if it's time to send a BLE update
      if (currentMillis - previousMillis >= bleInterval) {
        previousMillis = currentMillis;

        if (scale.is_ready()) {
          // Read and normalize the force value
          float pounds = scale.get_units(5) - baselineOffset;
          pounds = abs(pounds);                  // Take absolute value
          lastValidReading = pounds;             // Store latest value

          // Format BLE message: "<time>|<force>"
          char message[40];
          snprintf(message, sizeof(message), "%lu|%.1f", currentMillis - connectionMillis, pounds);
          forceDataCharacteristic.writeValue((uint8_t*)message, strlen(message)); // Send over BLE

          Serial.print("Sent: ");
          Serial.println(message);

          // If test is running, track peak force and end test if time elapsed
          if (testActive) {
            if (pounds > peakForce) {
              peakForce = pounds;
            }

            // End test after testDuration has passed
            if ((currentMillis - testStartTime) >= testDuration) {
              testActive = false;
              Serial.println("Test session complete.");
              Serial.print("Peak Force Recorded: ");
              Serial.print(peakForce, 1);
              Serial.println(" lbs");
              Serial.println("Press 'S' to start another test.");
            }
          }
        }
      }
    }

    // Once disconnected
    Serial.println("Disconnected from central.");
    connectionMillis = 0;         // Reset connection time
    lastValidReading = 0;         // Reset reading
    testActive = false;           // Reset test flag
  }
}
