/*
  Arduino GIGA R1 WiFi — RECEIVER (Forward-only)

  Roles:
    - CENTRAL to nRF "NRF_FORCE_1"
        * Service: 0x180C
        * Char   : 0x2A56
        * Payload: "<ms>|<lbs>"
        * Subscribes (if possible) AND actively polls every 100 ms
          with de-duplication so you get continuous updates reliably.
    - PERIPHERAL to MAIN "GIGA_RECEIVER"
        * Service: b39a1001-0f3b-4b6c-a8ad-5c8471a40101
        * Char   : b39a1002-0f3b-4b6c-a8ad-5c8471a40101  (Notify/Read)
        * Forwards the nRF payload unchanged to MAIN (Notify when subscribed).

  Serial (115200): Prints the forwarded line for quick verification.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

// -------- Upstream (nRF) --------
static const char* NRF_NAME = "NRF_FORCE_1";
#define NRF_SERVICE_UUID_STD "180C"
#define NRF_CHAR_UUID_STD    "2A56"

// -------- Downstream (to MAIN) --------
static const char* RECEIVER_NAME = "GIGA_RECEIVER";
#define FORWARD_SERVICE_UUID "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID    "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

// Peripheral (to MAIN)
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 48);

// Central handles (to nRF)
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

// Polling / de-dup
unsigned long lastPollMs = 0;
char          lastMsg[48] = {0};
int           lastMsgLen  = 0;

static const uint32_t SERIAL_BAUD = 115200;

// --- Debug helper if UUIDs mismatch ---
void dumpServices(BLEDevice& d) {
  Serial.println("[RECV] Services/Characteristics on NRF:");
  int svcCount = d.serviceCount();
  for (int i = 0; i < svcCount; i++) {
    BLEService s = d.service(i);
    Serial.print("  svc "); Serial.println(s.uuid());
    int chCount = s.characteristicCount();
    for (int j = 0; j < chCount; j++) {
      BLECharacteristic c = s.characteristic(j);
      Serial.print("    char "); Serial.print(c.uuid());
      Serial.print(" props:");
      if (c.canRead())      Serial.print(" R");
      if (c.canWrite())     Serial.print(" W");
      if (c.canSubscribe()) Serial.print(" N");
      Serial.println();
    }
  }
}

bool resolveNRFCharacteristic(BLEDevice& d, BLECharacteristic& out) {
  BLEService svcStd = d.service(NRF_SERVICE_UUID_STD);
  if (svcStd) {
    BLECharacteristic c2 = svcStd.characteristic(NRF_CHAR_UUID_STD);
    if (c2) { out = c2; return true; }
    // Fallback: first notifiable in 180C
    int chCount = svcStd.characteristicCount();
    for (int i = 0; i < chCount; i++) {
      BLECharacteristic c = svcStd.characteristic(i);
      if (c && c.canSubscribe()) { out = c; return true; }
    }
  }
  return false;
}

void setupPeripheral() {
  BLE.setLocalName(RECEIVER_NAME);
  BLE.setDeviceName(RECEIVER_NAME);

  // Advertise forward service (others still accessible after connect)
  BLE.setAdvertisedService(forwardService);

  forwardService.addCharacteristic(forwardChar);
  BLE.addService(forwardService);

  forwardChar.writeValue((const uint8_t*)"0|0.00", 6);

  BLE.advertise();  // advertise once
  Serial.println("[RECV] Advertising as GIGA_RECEIVER (Forward-only)...");
}

bool connectToNRF() {
  Serial.println("[RECV] Scanning for NRF_FORCE_1 ...");
  BLE.scan();

  while (true) {
    BLEDevice d = BLE.available();
    if (d && d.hasLocalName() && d.localName() == NRF_NAME) {
      Serial.println("[RECV] Found NRF, connecting...");
      BLE.stopScan();

      if (!d.connect()) {
        Serial.println("[RECV] connect() failed, rescanning...");
        BLE.scan();
        continue;
      }
      if (!d.discoverAttributes()) {
        Serial.println("[RECV] discoverAttributes() failed, rescanning...");
        d.disconnect();
        BLE.scan();
        continue;
      }

      BLECharacteristic c;
      if (!resolveNRFCharacteristic(d, c)) {
        Serial.println("[RECV] NRF char not found; dumping services:");
        dumpServices(d);
        d.disconnect();
        BLE.scan();
        continue;
      }

      if (c.canSubscribe()) {
        if (!c.subscribe()) {
          Serial.println("[RECV] NRF subscribe failed; will rely on polling.");
        } else {
          Serial.println("[RECV] Subscribed to NRF notifications.");
        }
      }

      nrfDev  = d;
      nrfChar = c;
      Serial.print("[RECV] Using NRF char "); Serial.println(nrfChar.uuid());

      lastPollMs = millis();
      lastMsg[0] = 0; lastMsgLen = 0;
      return true;
    }
    BLE.poll();
    delay(5);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 2000) {}

  if (!BLE.begin()) {
    Serial.println("[RECV] BLE.begin() failed");
    while (1) {}
  }

  setupPeripheral();
  connectToNRF();
}

void loop() {
  BLE.poll();

  // Reconnect to nRF if needed (rate-limited to avoid stack churn)
  if (!nrfDev || !nrfDev.connected()) {
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt > 1000) {
      lastAttempt = millis();
      Serial.println("[RECV] NRF disconnected; reconnecting...");
      connectToNRF();
    }
    return;
  }

  // Continuous stream: actively poll nRF every 100 ms, with de-dup
  unsigned long now = millis();
  if (now - lastPollMs >= 100) {
    lastPollMs = now;

    char buf[48] = {0};
    int n = nrfChar.readValue((uint8_t*)buf, sizeof(buf) - 1);  // poll read
    if (n > 0) {
      // De-duplicate to avoid resending unchanged values
      bool changed = (n != lastMsgLen) || memcmp(buf, lastMsg, n) != 0;
      if (changed) {
        forwardChar.writeValue((const uint8_t*)buf, n);  // notify if subscribed
        // Local debug
        Serial.print("[RECV] "); Serial.println(buf);
        // Save last
        memcpy(lastMsg, buf, n);
        lastMsg[n] = 0;
        lastMsgLen = n;
      }
    }
  }

  delay(1);
}
