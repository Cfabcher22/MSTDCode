/*
  Arduino GIGA R1 WiFi — MAIN (Print CSV)

  Connects to: "GIGA_RECEIVER"
  Subscribes to Forward Char:
    - Service: b39a1001-0f3b-4b6c-a8ad-5c8471a40101
    - Char   : b39a1002-0f3b-4b6c-a8ad-5c8471a40101 (Notify/Read)
  Payload: "<ms>|<lbs>"

  Serial (115200): prints "ms,pounds" then continuous CSV rows.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

static const char* RECEIVER_NAME = "GIGA_RECEIVER";
#define FORWARD_CHAR_UUID "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

BLEDevice         rxDev;
BLECharacteristic chForce;
bool headerPrinted = false;

bool connectToReceiver() {
  Serial.println("[MAIN] Scanning for GIGA_RECEIVER ...");
  BLE.scanForName(RECEIVER_NAME);

  while (true) {
    BLEDevice d = BLE.available();
    if (d && d.hasLocalName() && d.localName() == RECEIVER_NAME) {
      Serial.println("[MAIN] Found Receiver, connecting...");
      BLE.stopScan();

      if (!d.connect()) {
        Serial.println("[MAIN] connect() failed, rescanning...");
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }
      if (!d.discoverAttributes()) {
        Serial.println("[MAIN] discoverAttributes() failed, rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      BLECharacteristic f = d.characteristic(FORWARD_CHAR_UUID);
      if (!f) {
        Serial.println("[MAIN] Forward char missing; rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      if (!f.canSubscribe() || !f.subscribe()) {
        Serial.println("[MAIN] subscribe() failed; rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      rxDev   = d;
      chForce = f;
      Serial.println("[MAIN] Connected & subscribed.");
      headerPrinted = false;
      return true;
    }
    BLE.poll();
    delay(5);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  if (!BLE.begin()) {
    Serial.println("[MAIN] BLE.begin() failed");
    while (1) {}
  }

  connectToReceiver();
}

void loop() {
  BLE.poll();

  // Reconnect if needed
  if (!rxDev || !rxDev.connected()) {
    Serial.println("[MAIN] Receiver disconnected; reconnecting...");
    connectToReceiver();
    return;
  }

  // Print CSV header once
  if (!headerPrinted) {
    Serial.println("ms,pounds");
    headerPrinted = true;
  }

  // Handle incoming force data
  if (chForce && chForce.valueUpdated()) {
    char buf[48] = {0};
    int n = chForce.readValue((uint8_t*)buf, sizeof(buf)-1);
    if (n > 0) {
      char* bar = strchr(buf, '|');
      if (bar) {
        *bar = '\0';
        unsigned long ms = strtoul(buf, NULL, 10);
        float pounds = atof(bar + 1);
        Serial.print(ms); Serial.print(","); Serial.println(pounds, 2);
      } else {
        // If it's not delimited, just dump it for debugging
        Serial.print("#WARN raw: "); Serial.println(buf);
      }
    }
  }

  delay(1);
}
