#include <ArduinoBLE.h>

// === Constants ===
const char* targetDeviceName = "Force Sensing Unit 1";   // nRF peripheral name
const char* forceServiceUUID = "180C";                   // Service UUID on nRF
const char* forceCharUUID = "2A56";                      // Characteristic UUID on nRF


// === Central Role BLE Objects (GIGA1 → nRF) ===
BLEDevice peripheral;
BLECharacteristic forceCharacteristic;

// === Peripheral Role BLE Objects (GIGA1 to GIGA2) ===
BLEService forwardService("190A");  // Custom service for forwarding
BLECharacteristic forwardCharacteristic("2BA1", BLERead | BLENotify, 50);

// === Forwarding Buffer ===
char forwardBuffer[50] = {0};
bool newDataToForward = false;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Start BLE hardware
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  // === Setup Peripheral Role (GIGA1 to GIGA2) ===
  BLE.setLocalName("GIGA_FORWARDER");
  BLE.setAdvertisedService(forwardService);
  forwardService.addCharacteristic(forwardCharacteristic);
  BLE.addService(forwardService);
  forwardCharacteristic.writeValue("Waiting...");

  BLE.advertise();  // Always advertising for GIGA2
  Serial.println("GIGA1 advertising as 'GIGA_FORWARDER'");

  // === Start scanning for nRF peripheral ===
  BLE.scan();
  Serial.println("Scanning for Force Sensing Unit...");
}

void loop() {
  BLE.poll();  // Handle both central and peripheral roles

  // === CENTRAL ROLE: Connect to nRF and read force data ===
  if (!peripheral || !peripheral.connected()) {
    peripheral = BLE.available();

    if (peripheral && peripheral.localName() == targetDeviceName) {
      Serial.print("Found ");
      Serial.println(targetDeviceName);

      BLE.stopScan();

      if (peripheral.connect()) {
        Serial.println("Connected to Force Sensing Unit");

        if (peripheral.discoverAttributes()) {
          forceCharacteristic = peripheral.characteristic(forceCharUUID);

          if (forceCharacteristic && forceCharacteristic.canSubscribe()) {
            forceCharacteristic.subscribe();
            Serial.println("Subscribed to force data");
          } else {
            Serial.println("Force characteristic not found or unsubscribable");
            peripheral.disconnect();
          }
        } else {
          Serial.println("Failed to discover attributes");
          peripheral.disconnect();
        }
      } else {
        Serial.println("Failed to connect to peripheral");
        BLE.scan();
      }
    }
  }

  // === Data received from nRF (central role) ===
  if (peripheral.connected() && forceCharacteristic.valueUpdated()) {
    int len = forceCharacteristic.valueLength();
    len = min(len, sizeof(forwardBuffer) - 1);  // Prevent overflow

    memcpy(forwardBuffer, forceCharacteristic.value(), len);
    forwardBuffer[len] = '\0';  // Null-terminate string

    Serial.print("Received from nRF: ");
    Serial.println(forwardBuffer);

    newDataToForward = true;
  }

  // === Forward data to GIGA2 (peripheral role) ===
  if (newDataToForward && forwardCharacteristic.subscribed()) {
    forwardCharacteristic.writeValue((uint8_t*)forwardBuffer, strlen(forwardBuffer));
    newDataToForward = false;
  }
}
