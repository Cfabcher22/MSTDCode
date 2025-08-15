#include <ArduinoBLE.h>

// === GIGA 1 (forwarder) BLE identifiers ===
const char* forwarderName = "GIGA_FORWARDER";
const char* forwardServiceUUID = "190A";
const char* forwardCharUUID = "2BA1";

// === BLE Objects ===
BLEDevice forwarder;
BLECharacteristic forwardChar;

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for Serial Monitor

  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    while (1);
  }

  Serial.println("Scanning for GIGA_FORWARDER...");
  BLE.scan();
}

void loop() {
  BLE.poll();  // Keep BLE stack alive

  if (!forwarder || !forwarder.connected()) {
    forwarder = BLE.available();

    if (forwarder && forwarder.localName() == forwarderName) {
      Serial.println("Found GIGA_FORWARDER. Connecting...");

      BLE.stopScan();

      if (!forwarder.connect()) {
        Serial.println("Connection failed. Retrying...");
        BLE.scan();
        return;
      }

      if (!forwarder.discoverAttributes()) {
        Serial.println("Attribute discovery failed.");
        forwarder.disconnect();
        BLE.scan();
        return;
      }

      forwardChar = forwarder.characteristic(forwardCharUUID);

      if (!forwardChar || !forwardChar.canSubscribe()) {
        Serial.println("Characteristic not found or not subscribable.");
        forwarder.disconnect();
        BLE.scan();
        return;
      }

      forwardChar.subscribe();
      Serial.println("Subscribed to forwarded force data.");
    }
  }

  if (forwarder && forwarder.connected() && forwardChar.valueUpdated()) {
    int len = forwardChar.valueLength();
    uint8_t buffer[51] = {0};  // One extra byte for null-terminator

    forwardChar.readValue(buffer, len);
    buffer[len] = '\0';  // Null-terminate for safe string printing

    Serial.print("Forwarded data: ");
    Serial.println((char*)buffer);
  }
}
