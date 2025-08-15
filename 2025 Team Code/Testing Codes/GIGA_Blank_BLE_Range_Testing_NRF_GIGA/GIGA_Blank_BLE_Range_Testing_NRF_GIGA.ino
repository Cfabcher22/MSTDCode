#include <ArduinoBLE.h>

// Target peripheral name and UUIDs
const char* targetDeviceName = "XIAO-PING";
const char* serviceUUID = "180C";
const char* charUUID = "2A56";

// BLE objects
BLEDevice peripheral;
BLECharacteristic pingChar;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!BLE.begin()) {
    Serial.println("Failed to initialize BLE!");
    while (1);
  }

  Serial.println("GIGA Central - BLE Range Tester");
  BLE.scan();  // Start scanning
}

void loop() {
  // Look for devices during scanning
  BLEDevice device = BLE.available();
  if (device) {
    if (device.localName() == targetDeviceName) {
      Serial.println("Found target device. Connecting...");

      BLE.stopScan();  // Stop scanning before connecting
      if (device.connect()) {
        Serial.println("Connected to XIAO");
        
        if (device.discoverAttributes()) {
          BLECharacteristic characteristic = device.characteristic(charUUID);
          
          if (characteristic) {
            Serial.println("Characteristic found. Subscribing...");
            characteristic.subscribe();
            pingChar = characteristic;
            peripheral = device;
          } else {
            Serial.println("Characteristic not found.");
            device.disconnect();
          }
        } else {
          Serial.println("Service discovery failed.");
          device.disconnect();
        }
      } else {
        Serial.println("Connection failed.");
      }
    }
  }

  // Read and print value if connected and updated
  if (peripheral && peripheral.connected() && pingChar) {
    if (pingChar.valueUpdated()) {
      int length = pingChar.valueLength();
      uint8_t buffer[20];
      pingChar.readValue(buffer, length);

      buffer[length] = '\0'; // Null terminate
      Serial.print("Received: ");
      Serial.println((char*)buffer);
    }
  }

  // Reconnect if disconnected
  if (peripheral && !peripheral.connected()) {
    Serial.println("Disconnected. Restarting scan...");
    peripheral = BLEDevice();  // Clear reference
    BLE.scan();                // Restart scanning
  }

  BLE.poll();
}
