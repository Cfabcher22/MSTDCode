// Include the ArduinoBLE library to enable Bluetooth Low Energy communication
#include <ArduinoBLE.h>

// === Constants ===
// These UUIDs must match the service and characteristic from the BLE peripheral (e.g., your nRF52840 board)
const char* targetDeviceName = "Force Sensing Unit 1";   // Name of the peripheral you're trying to connect to
const char* forceServiceUUID = "180C";                   // UUID of the custom BLE service (for organization)
const char* forceCharUUID = "2A56";                      // UUID of the characteristic sending force data

// === Global BLE Objects ===
BLEDevice peripheral;                // Represents the connected BLE peripheral device
BLECharacteristic forceCharacteristic;  // Will hold the characteristic used to read force data from the peripheral

// === SETUP ===
void setup() {
  Serial.begin(115200);             // Start the serial monitor at 115200 baud
  while (!Serial);                  // Wait until the serial monitor is connected (important for GIGA boards)

  // Initialize the BLE hardware
  if (!BLE.begin()) {               // If initialization fails, halt execution
    Serial.println("Starting BLE failed!");
    while (1);                      // Infinite loop to prevent further code execution
  }

  Serial.println("Scanning for peripheral...");

  // Start actively scanning for available BLE devices
  BLE.scan();
}

// === LOOP ===
void loop() {
  // If not connected to a peripheral yet, try to find and connect
  if (!peripheral || !peripheral.connected()) {
    peripheral = BLE.available();  // Check if a BLE device is available during scan

    // Check if the discovered device matches the name we're looking for
    if (peripheral && peripheral.localName() == targetDeviceName) {
      Serial.print("Found ");
      Serial.println(targetDeviceName);

      BLE.stopScan();  // Stop scanning once we find the correct peripheral

      // Attempt to connect to the discovered device
      if (peripheral.connect()) {
        Serial.println("Connected to peripheral");

        // Discover all available services and characteristics on the connected device
        if (peripheral.discoverAttributes()) {
          // Retrieve the characteristic that will send force data
          forceCharacteristic = peripheral.characteristic(forceCharUUID);

          // If the characteristic is found, subscribe to its notifications
          if (forceCharacteristic) {
            forceCharacteristic.subscribe();  // Enable notifications from this characteristic
            Serial.println("Subscribed to force data notifications");
          } else {
            Serial.println("Force characteristic not found");
          }
        } else {
          Serial.println("Attribute discovery failed");  // Services or characteristics couldn't be read
        }
      } else {
        Serial.println("Failed to connect");  // BLE.connect() failed
        BLE.scan();                          // Restart scanning for devices
      }
    }
  }

  // If connected and new data was received via BLE notification
  if (peripheral.connected() && forceCharacteristic.valueUpdated()) {
    int len = forceCharacteristic.valueLength();  // Get the length of the received data
    char buffer[50];                              // Allocate a buffer to store incoming string
    memcpy(buffer, forceCharacteristic.value(), len);  // Copy the raw data into the buffer
    buffer[len] = '\0';                           // Null-terminate to make it a valid C-string

    Serial.print("Received: ");
    Serial.println(buffer);                      // Display the received message in the serial monitor
  }
}
