/*
  Arduino GIGA R1 WiFi — BLE Central with status prints + CSV data

  Target peripheral:
    Name         : "NRF_FORCE_1"
    Service UUID : 180C
    Char UUID    : 2A56
    Payload      : "ms_since_connect|pounds"

  Serial output:
    # GIGA: SCANNING (waiting for advertising)...
    # GIGA: ADVERTISING detected from "NRF_FORCE_1" addr=XX:XX:...
    # GIGA: CONNECT ATTEMPT...
    # GIGA: CONNECTED.
    # GIGA: SUBSCRIBED.
    # GIGA: DISCONNECTED. Rescanning...
    (data) ms,pounds

  Data lines are CSV only:  ms,pounds
*/

#include <ArduinoBLE.h>

const char* TARGET_NAME  = "NRF_FORCE_1";
const char* SERVICE_UUID = "180C";
const char* CHAR_UUID    = "2A56";

BLEDevice         peripheral;
BLECharacteristic forceChar;

bool headerPrinted     = false;
bool scanningAnnounced = false;

// ---------- Helpers ----------
bool hasService(BLEDevice& dev, const char* uuid) {
  int n = dev.advertisedServiceUuidCount();
  for (int i = 0; i < n; i++) {
    if (dev.advertisedServiceUuid(i) == uuid) return true;
  }
  return false;
}

bool connectAndSubscribe(BLEDevice& dev) {
  Serial.println("# GIGA: CONNECT ATTEMPT...");
  if (!dev.connect()) {
    Serial.println("# GIGA: connect() failed");
    return false;
  }

  Serial.println("# GIGA: CONNECTED. Discovering attributes...");
  if (!dev.discoverAttributes()) {
    Serial.println("# GIGA: discoverAttributes() failed");
    dev.disconnect();
    return false;
  }

  forceChar = dev.characteristic(CHAR_UUID);
  if (!forceChar) {
    Serial.println("# GIGA: characteristic 2A56 not found");
    dev.disconnect();
    return false;
  }

  if (!forceChar.canSubscribe()) {
    Serial.println("# GIGA: characteristic not subscribable");
    dev.disconnect();
    return false;
  }

  if (!forceChar.subscribe()) {
    Serial.println("# GIGA: subscribe() failed");
    dev.disconnect();
    return false;
  }

  Serial.println("# GIGA: SUBSCRIBED.");
  return true;
}

// Optional: event handlers (extra clarity on disconnects)
void onBLEDisconnected(BLEDevice dev) {
  Serial.println("# GIGA: DISCONNECTED. Rescanning...");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* wait up to 3s */ }

  if (!BLE.begin()) {
    Serial.println("# GIGA: BLE.begin() failed");
    while (1) {}
  }

  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);

  Serial.println("# GIGA: SCANNING (waiting for advertising)...");
  BLE.scanForName(TARGET_NAME);      // you can also use BLE.scan() and filter by service
  scanningAnnounced = true;
}

void loop() {
  // If not connected, keep scanning until we see the target and can connect.
  if (!peripheral || !peripheral.connected()) {
    BLEDevice found = BLE.available();
    if (found) {
      Serial.print("# GIGA: ADVERTISING detected from \"");
      Serial.print(found.localName());
      Serial.print("\" addr=");
      Serial.println(found.address());

      if (found.localName() == TARGET_NAME || hasService(found, SERVICE_UUID)) {
        BLE.stopScan();
        if (connectAndSubscribe(found)) {
          peripheral = found;
          headerPrinted     = false; // print CSV header once per session
          scanningAnnounced = false; // so we can announce scanning again after disconnect
        } else {
          // Resume scanning if connection/subscription failed
          Serial.println("# GIGA: Resuming scan...");
          BLE.scanForName(TARGET_NAME);
          scanningAnnounced = true;
        }
      }
    } else {
      // Periodically remind we're scanning (not too chatty)
      static unsigned long lastScanMsg = 0;
      if (!scanningAnnounced || millis() - lastScanMsg > 5000) {
        Serial.println("# GIGA: SCANNING (waiting for advertising)...");
        scanningAnnounced = true;
        lastScanMsg = millis();
      }
    }
    return; // continue scanning
  }

  // Connected: handle notifications only (print data as CSV)
  BLE.poll();

  if (!peripheral.connected()) {
    // Event handler already printed a line; restart scanning.
    BLE.scanForName(TARGET_NAME);
    scanningAnnounced = false;
    return;
  }

  if (!headerPrinted) {
    Serial.println("ms,pounds");
    headerPrinted = true;
  }

  if (forceChar && forceChar.valueUpdated()) {
    char buf[64] = {0};
    int n = forceChar.readValue((uint8_t*)buf, sizeof(buf) - 1);
    if (n > 0) {
      // Expected "ms|pounds"
      char *bar = strchr(buf, '|');
      if (bar) {
        *bar = '\0';                         // split in-place
        unsigned long ms = strtoul(buf, NULL, 10);
        float pounds = atof(bar + 1);

        // CSV data line
        Serial.print(ms);
        Serial.print(",");
        Serial.println(pounds, 2);
      }
    }
  }
}
