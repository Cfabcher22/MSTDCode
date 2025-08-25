/*
  GIGA_FORWARDER (Dual-Role BLE Bridge)
  -------------------------------------
  SUMMARY
  - Acts as a BLE *central* to an nRF52840 peripheral named "Force Sensing Unit 1".
    * Connects, discovers attributes, and subscribes to a force data characteristic.
    * Reads incoming force strings from the nRF (e.g., "BASE:123.45").
  - Simultaneously acts as a BLE *peripheral* to a second device (e.g., GIGA2).
    * Advertises a custom service/characteristic and forwards received force strings
      by notifying subscribers.

  FLOW
  1) Start BLE, advertise the forward service for GIGA2, and begin scanning for the nRF.
  2) When the nRF is found by name, connect and subscribe to its force characteristic.
  3) When new force data arrives from the nRF, copy it into a buffer.
  4) If a subscriber (GIGA2) is listening, notify it with the buffered data.

  VARIABLE / CONSTANT KEY
  - targetDeviceName      : GAP name of the nRF peripheral to connect to.
  - forceServiceUUID      : (Not directly used here, but documents expected service).
  - forceCharUUID         : UUID of the force data characteristic on the nRF.
  - peripheral            : BLEDevice handle for the connected nRF.
  - forceCharacteristic   : Handle to the nRF force characteristic (subscribed).
  - forwardService (190A) : Local service we advertise for downstream device.
  - forwardCharacteristic : Local notify characteristic used to forward strings.
  - forwardBuffer[50]     : Staging buffer for latest force string.
  - newDataToForward      : Flag indicating buffer holds fresh data to notify.
*/

#include <ArduinoBLE.h>
#include <cstring>    // for memcpy (safe buffer copy)

// === Constants ===
const char* targetDeviceName = "Force Sensing Unit 1";   // nRF peripheral name to match
const char* forceServiceUUID = "180C";                   // Expected service on nRF (documentary)
const char* forceCharUUID = "2A56";                      // Force characteristic UUID on nRF

// === Central Role BLE Objects (GIGA1 → nRF) ===
BLEDevice peripheral;            // Remote device handle (the nRF)
BLECharacteristic forceCharacteristic;  // Remote characteristic for force data

// === Peripheral Role BLE Objects (GIGA1 to GIGA2) ===
BLEService forwardService("190A");                                  // Custom service for forwarding
BLECharacteristic forwardCharacteristic("2BA1", BLERead | BLENotify, 50); // Notified string to GIGA2

// === Forwarding Buffer ===
char forwardBuffer[50] = {0};    // Holds the most recent force string
bool newDataToForward = false;   // True when forwardBuffer has new content

void setup() {
  Serial.begin(115200);                      // Serial for logs
  while (!Serial);                           // Wait for serial monitor (optional on some boards)

  // Start BLE hardware
  if (!BLE.begin()) {                        // Initialize BLE stack
    Serial.println("Starting BLE failed!");
    while (1);                               // Halt if BLE cannot start
  }

  // === Setup Peripheral Role (GIGA1 to GIGA2) ===
  BLE.setLocalName("GIGA_FORWARDER");        // GAP name advertised
  BLE.setAdvertisedService(forwardService);  // Include our custom service in advertisements
  forwardService.addCharacteristic(forwardCharacteristic); // Attach notify characteristic
  BLE.addService(forwardService);            // Register service with the peripheral
  forwardCharacteristic.writeValue("Waiting..."); // Default payload so readers see something

  BLE.advertise();                           // Begin advertising so GIGA2 can find us
  Serial.println("GIGA1 advertising as 'GIGA_FORWARDER'");

  // === Start scanning for nRF peripheral ===
  BLE.scan();                                // Enter central scan mode
  Serial.println("Scanning for Force Sensing Unit...");
}

void loop() {
  BLE.poll();                                // Service BLE events for both roles

  // === CENTRAL ROLE: Connect to nRF and read force data ===
  if (!peripheral || !peripheral.connected()) {        // If not connected to nRF yet
    peripheral = BLE.available();                      // Check for scanned devices

    // Match by advertised local name
    if (peripheral && peripheral.localName() == targetDeviceName) {
      Serial.print("Found ");
      Serial.println(targetDeviceName);

      BLE.stopScan();                                  // Stop scanning before connecting

      if (peripheral.connect()) {                      // Attempt connection
        Serial.println("Connected to Force Sensing Unit");

        if (peripheral.discoverAttributes()) {         // Discover services/chars
          // Grab the force characteristic by UUID (service UUID not strictly required here)
          forceCharacteristic = peripheral.characteristic(forceCharUUID);

          // Subscribe if possible (notifications or indications)
          if (forceCharacteristic && forceCharacteristic.canSubscribe()) {
            forceCharacteristic.subscribe();           // Enable notifications
            Serial.println("Subscribed to force data");
          } else {
            Serial.println("Force characteristic not found or unsubscribable");
            peripheral.disconnect();                   // Clean up and try again later
          }
        } else {
          Serial.println("Failed to discover attributes");
          peripheral.disconnect();
        }
      } else {
        Serial.println("Failed to connect to peripheral");
        BLE.scan();                                    // Resume scanning if connection fails
      }
    }
  }

  // === Data received from nRF (central role) ===
  if (peripheral.connected() && forceCharacteristic.valueUpdated()) {
    int len = forceCharacteristic.valueLength();                   // Bytes in this update
    len = min(len, (int)sizeof(forwardBuffer) - 1);                // Prevent overflow

    memcpy(forwardBuffer, forceCharacteristic.value(), len);       // Copy payload
    forwardBuffer[len] = '\0';                                     // Ensure C-string

    Serial.print("Received from nRF: ");
    Serial.println(forwardBuffer);

    newDataToForward = true;                                       // Mark as fresh data
  }

  // === Forward data to GIGA2 (peripheral role) ===
  if (newDataToForward && forwardCharacteristic.subscribed()) {    // Only notify if subscribed
    forwardCharacteristic.writeValue((uint8_t*)forwardBuffer, strlen(forwardBuffer)); // Notify
    newDataToForward = false;                                       // Clear flag after send
  }
}
