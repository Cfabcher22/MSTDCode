#include <ArduinoBLE.h>

BLEService textService("180C");  // Custom service
BLECharacteristic textChar("2A56", BLERead | BLENotify, 50);

void setup() {
  // No Serial needed for auto-start
  BLE.begin();

  BLE.setLocalName("GIGA_BLE_SENDER");
  BLE.setAdvertisedService(textService);
  textService.addCharacteristic(textChar);
  BLE.addService(textService);
  textChar.writeValue("Waiting...");

  BLE.advertise();
}

void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    while (central.connected()) {
      const char* msg = "Hello from Peripheral";
      textChar.writeValue(msg);
      delay(1000);  // Send every second
    }
  }
}
