/*
  GIGA R1 WiFi — MAIN FORWARDER
  Role: Central (to NRF) + Peripheral (to RECEIVER GIGA)

  Upstream (central):
    - Connects to peripheral named "NRF_FORCE_1"
    - Subscribes to char b39a0002-0f3b-4b6c-a8ad-5c8471a40001 (ASCII "<ms>|<lbs>")

  Downstream (peripheral we expose):
    - Advertises as "GIGA_FORWARDER"
    - Service: b39a1001-0f3b-4b6c-a8ad-5c8471a40101
    - Char:    b39a1002-0f3b-4b6c-a8ad-5c8471a40101 (BLERead|BLENotify, 50 bytes)
    - Forwards upstream payload unchanged to subscribers

  Notes:
    - Keeps trying to (re)connect to NRF if link drops.
    - Keeps advertising the forward service for the RECEIVER GIGA.
*/

#include <ArduinoBLE.h>
#include <string.h>
#include <stdlib.h>

// ---- IDs for the NRF peripheral (upstream) ----
static const char* NRF_NAME           = "NRF_FORCE_1";
#define NRF_SERVICE_UUID              "b39a0001-0f3b-4b6c-a8ad-5c8471a40001"
#define NRF_CHAR_UUID                 "b39a0002-0f3b-4b6c-a8ad-5c8471a40001"

// ---- IDs we expose for the RECEIVER GIGA (downstream) ----
static const char* FORWARDER_NAME     = "GIGA_FORWARDER";
#define FORWARD_SERVICE_UUID          "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID             "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

// ---- Debug toggle ----
#define SERIAL_DEBUG 1

// ---- Downstream (peripheral) objects ----
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 50);

// ---- Upstream (central) handles ----
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

bool connectToNRF() {
  // Start/ensure scanning
  BLE.scan();
  if (SERIAL_DEBUG) Serial.println("FWD: scanning for NRF_FORCE_1 ...");

  // Non-blocking-ish loop to find and connect
  while (true) {
    BLEDevice found = BLE.available();
    if (found) {
      // Match by name (you can also add service UUID check if desired)
      if (found.hasLocalName() && found.localName() == NRF_NAME) {
        if (SERIAL_DEBUG) Serial.println("FWD: found NRF, connecting...");
        BLE.stopScan();

        if (!found.connect()) {
          if (SERIAL_DEBUG) Serial.println("FWD: connect failed; rescanning...");
          BLE.scan();
          continue;
        }
        if (!found.discoverAttributes()) {
          if (SERIAL_DEBUG) Serial.println("FWD: attr discovery failed; rescanning...");
          found.disconnect();
          BLE.scan();
          continue;
        }

        BLECharacteristic c = found.characteristic(NRF_CHAR_UUID);
        if (!c) {
          if (SERIAL_DEBUG) Serial.println("FWD: NRF char not found; rescanning...");
          found.disconnect();
          BLE.scan();
          continue;
        }
        if (!c.canSubscribe() || !c.subscribe()) {
          if (SERIAL_DEBUG) Serial.println("FWD: NRF subscribe failed; rescanning...");
          found.disconnect();
          BLE.scan();
          continue;
        }

        nrfDev  = found;
        nrfChar = c;

        if (SERIAL_DEBUG) {
          Serial.println("FWD: connected to NRF & subscribed.");
        }

        // Resume scanning OFF (we don't need it now)
        return true;
      }
    }

    BLE.poll();   // keep the BLE stack (incl. our peripheral role) serviced
    delay(5);
  }
}

void setupForwardPeripheral() {
  BLE.setLocalName(FORWARDER_NAME);
  BLE.setDeviceName(FORWARDER_NAME);
  BLE.setAdvertisedService(forwardService);

  forwardService.addCharacteristic(forwardChar);
  BLE.addService(forwardService);

  // Seed initial value
  const char *initMsg = "0|0.0";
  forwardChar.writeValue((const uint8_t*)initMsg, strlen(initMsg));

  BLE.advertise();
  if (SERIAL_DEBUG) Serial.println("FWD: advertising as GIGA_FORWARDER");
}

void setup() {
  if (SERIAL_DEBUG) {
    Serial.begin(115200);
    while (!Serial) {}
  }

  if (!BLE.begin()) {
    if (SERIAL_DEBUG) Serial.println("FWD: BLE init failed");
    while (1) {}
  }

  // Set up our downstream peripheral first so receivers can connect anytime
  setupForwardPeripheral();

  // Then establish upstream link to NRF
  connectToNRF();
}

void loop() {
  BLE.poll();

  // (Re)connect to NRF if needed
  if (!nrfDev || !nrfDev.connected()) {
    if (SERIAL_DEBUG) Serial.println("FWD: NRF disconnected; reconnecting...");
    // Ensure we’re still advertising to receivers
    BLE.advertise();
    connectToNRF();
    return;
  }

  // Forward any new data from NRF to our forward characteristic
  if (nrfChar && nrfChar.valueUpdated()) {
    int len = nrfChar.valueLength();
    if (len > 0 && len <= 50) {
      uint8_t buf[51] = {0};
      nrfChar.readValue(buf, len);
      buf[len] = '\0';

      // Forward unchanged; receivers subscribed to forwardChar get a Notify
      forwardChar.writeValue(buf, len);

      if (SERIAL_DEBUG) {
        Serial.print("FWD: ");
        Serial.println((char*)buf);
      }
    }
  }

  // Keep advertising alive (idempotent; safe to call occasionally)
  static uint32_t lastAdvBump = 0;
  uint32_t now = millis();
  if (now - lastAdvBump > 5000) {
    BLE.advertise();
    lastAdvBump = now;
  }

  delay(2);
}
