#include <ArduinoBLE.h>

BLEDevice peripheral;
BLECharacteristic textChar;

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for Serial Monitor (optional)

  BLE.begin();
  BLE.scan();  // Start scanning immediately
}

void loop() {
  BLEDevice found = BLE.available();

  if (found && found.localName() == "GIGA_BLE_SENDER") {
    BLE.stopScan();

    if (!found.connect()) {
      BLE.scan();  // Retry scan
      return;
    }

    if (!found.discoverAttributes()) {
      found.disconnect();
      BLE.scan();
      return;
    }

    textChar = found.characteristic("2A56");

    if (!textChar || !textChar.canSubscribe()) {
      found.disconnect();
      BLE.scan();
      return;
    }

    textChar.subscribe();

    while (found.connected()) {
      BLE.poll();
      if (textChar.valueUpdated()) {
        int len = textChar.valueLength();
        uint8_t buffer[51] = {0};  // Leave space for null terminator
        textChar.readValue(buffer, len);
        buffer[len] = '\0';  // Safely null-terminate
        Serial.print("Received: ");
        Serial.println((char*)buffer);
      }
    }

    BLE.scan();  // Start scanning again after disconnect
  }
}
