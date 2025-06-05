/*
  BLE Central Sketch for Arduino Giga
  Scans for and connects to the "Force Sensing Unit 1" (XIAO nRF52840 peripheral)
  Reads force data from characteristic "2A56".
*/

#include <ArduinoBLE.h>

// === GLOBAL VARIABLES ===
BLEDevice peripheral;                     // Represents the peripheral device
BLECharacteristic messageCharacteristic;  // Characteristic to read force data from

unsigned long connectionMillis = 0;   // Timestamp when connection was established
unsigned long lastMessageMillis = 0;  // Timestamp of the last received message

// Target peripheral name and characteristic UUID
const char* targetDeviceName = "Force Sensing Unit 1";
const char* targetCharacteristicUUID = "2A56";

void setup() {
  Serial.begin(115200);
  // It's good practice to wait for Serial to connect, but for a standalone device,
  // you might want to remove or shorten this for faster startup on battery.
  // For debugging, it's fine.
  unsigned long serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 5000)); // Wait up to 5 seconds for serial

  Serial.println("BLE Central - Initializing...");

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    // Indicate critical failure (e.g., onboard LED if available, or just halt)
    while (1);
  }

  Serial.println("BLE Central - Scanning for peripherals...");
  startScanning();
}

void loop() {
  // Check if a peripheral is available and not yet connected
  if (!peripheral || !peripheral.connected()) {
    BLEDevice foundPeripheral = BLE.available();

    if (foundPeripheral) {
      Serial.print("Found device: ");
      Serial.print(foundPeripheral.address());
      Serial.print(" '");
      Serial.print(foundPeripheral.localName());
      Serial.println("'");

      // Check if it's the device we're looking for
      if (foundPeripheral.localName() == targetDeviceName) {
        Serial.print("Found ");
        Serial.print(targetDeviceName);
        Serial.println(". Attempting to connect...");
        
        BLE.stopScan(); // Stop scanning once we've found our target

        if (connectToPeripheral(foundPeripheral)) {
          Serial.println("Successfully connected!");
          connectionMillis = millis();
          lastMessageMillis = 0; // Reset last message time

          // Discover attributes and subscribe to the characteristic
          if (discoverAndSubscribe(peripheral)) {
            Serial.println("Subscribed to characteristic. Ready to receive data.");
          } else {
            Serial.println("Failed to subscribe to characteristic. Disconnecting.");
            peripheral.disconnect();
            // No need to call startScanning() here, the outer loop will handle it.
          }
        } else {
          Serial.println("Connection failed. Resuming scan...");
          startScanning(); // Resume scanning if connection failed
        }
      }
    }
  } else { // Peripheral is presumably connected
    // Check if the peripheral is still connected
    if (peripheral.connected()) {
      // Check if the characteristic has new data
      if (messageCharacteristic && messageCharacteristic.valueUpdated()) {
        processCharacteristicData();
      }
    } else {
      // Peripheral disconnected
      Serial.println("Peripheral disconnected. Scanning...");
      peripheral = BLEDevice(); // Clear the peripheral object
      messageCharacteristic = BLECharacteristic(); // Clear characteristic
      startScanning();
    }
  }
  
  // A small delay can be good to prevent tight looping,
  // but BLE.poll() (called internally by library functions like BLE.available())
  // handles BLE events.
  // delay(50); 
}

// Function to start or restart BLE scanning
void startScanning() {
  Serial.println("Scanning for BLE Peripherals...");
  // Configure scan parameters if needed (e.g., scan interval, window)
  // BLE.setScanInterval(interval, window); 
  if (!BLE.scanForName(targetDeviceName)) {
    Serial.println("ERROR: Failed to start scanning (perhaps BLE not initialized properly).");
    // Consider a retry mechanism or error indication
  }
}

// Function to connect to the peripheral
bool connectToPeripheral(BLEDevice deviceToConnect) {
  if (deviceToConnect.connect()) {
    peripheral = deviceToConnect; // Store the connected peripheral globally
    return true;
  }
  return false;
}

// Function to discover attributes and subscribe to the characteristic
bool discoverAndSubscribe(BLEDevice connectedDevice) {
  Serial.println("Discovering attributes...");
  if (connectedDevice.discoverAttributes()) {
    Serial.println("Attributes discovered.");
    messageCharacteristic = connectedDevice.characteristic(targetCharacteristicUUID);

    if (!messageCharacteristic) {
      Serial.print("Characteristic ");
      Serial.print(targetCharacteristicUUID);
      Serial.println(" not found!");
      return false;
    }

    if (!messageCharacteristic.canSubscribe()) {
      Serial.println("Characteristic cannot be subscribed to!");
      return false;
    }

    if (messageCharacteristic.subscribe()) {
      Serial.println("Subscription successful!");
      return true;
    } else {
      Serial.println("Subscription failed!");
      return false;
    }
  } else {
    Serial.println("Attribute discovery failed!");
    return false;
  }
}

// Function to process data received from the characteristic
void processCharacteristicData() {
  int length = messageCharacteristic.valueLength();
  uint8_t buffer[length + 1]; // +1 for null terminator

  messageCharacteristic.readValue(buffer, length);
  buffer[length] = '\0'; // Null-terminate the string

  String message = (char*)buffer;
  int separatorIndex = message.indexOf('|');

  if (separatorIndex != -1) {
    String timestampStr = message.substring(0, separatorIndex); // Peripheral's timestamp
    String poundsStr = message.substring(separatorIndex + 1);

    float pounds = poundsStr.toFloat();
    unsigned long centralTimestamp = millis(); // Central's timestamp when message is processed

    Serial.println("----------------------------------------------------");
    Serial.print("Force: ");
    Serial.print(pounds, 2); // Print with 2 decimal places
    Serial.println(" lbs");

    Serial.print("Peripheral Uptime (ms): ");
    Serial.println(timestampStr);

    Serial.print("Central Time Since Connection (ms): ");
    Serial.println(centralTimestamp - connectionMillis);

    if (lastMessageMillis > 0) {
      Serial.print("Central Time Since Last Message (ms): ");
      Serial.println(centralTimestamp - lastMessageMillis);
    } else {
      Serial.println("First message received.");
    }

    lastMessageMillis = centralTimestamp;
    Serial.println("----------------------------------------------------");
  } else {
    Serial.print("Malformed message received: ");
    Serial.println(message);
  }
}
