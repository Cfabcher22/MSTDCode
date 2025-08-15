/*
  GIGA R1 WiFi — RECEIVER (stabilized)
  - CENTRAL to nRF "NRF_FORCE_1" (180C/2A56) -> polls every 100 ms for "<ms>|<lbs>"
  - PERIPHERAL to MAIN "GIGA_RECEIVER":
      * Force Forward (Notify): b39a1002-0f3b-4b6c-a8ad-5c8471a40101
      * Simple ping-pong (every 3 s after connect):
          - Receiver -> Main (Notify TX): "Hello MSTD Team"
          - Main replies (Write RX): "Devil Dog!"
  Changes:
    * Gate all notifies until MAIN is connected (prevents crashes / red LED flashing)
    * Remove periodic advertise() spam; advertise once and rely on stack
    * Wait 1s after MAIN connection before first hello
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

// ---------- Upstream (nRF as peripheral) ----------
static const char* NRF_NAME = "NRF_FORCE_1";
#define NRF_SERVICE_UUID_STD "180C"
#define NRF_CHAR_UUID_STD    "2A56"

// ---------- Downstream (to Main as our central) ----------
static const char* RECEIVER_NAME = "GIGA_RECEIVER";

// Force Forward service/char
#define FORWARD_SERVICE_UUID "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID    "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

// UART-like tiny ping/pong (Nordic NUS style)
#define UART_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Notify to Main
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Write  from Main

// ---------- Peripheral objects (to Main) ----------
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 48);

BLEService        uartService(UART_SERVICE_UUID);
BLECharacteristic uartTX(UART_TX_UUID, BLERead | BLENotify, 24);
BLECharacteristic uartRX(UART_RX_UUID, BLEWrite | BLEWriteWithoutResponse, 24);

// ---------- Central handles (to nRF) ----------
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

// ---------- Main GIGA connection state ----------
volatile bool mainConnected = false;
unsigned long mainConnectedAt = 0;   // ms timestamp when MAIN connected

// ---------- Timers ----------
unsigned long lastPollMs  = 0;  // nRF read poll
unsigned long lastHelloMs = 0;  // periodic Hello

// ---- UART helpers (keep ≤20B/packet) ----
static inline void notifySmall(const char* s) {
  size_t len = strlen(s);
  const size_t CHUNK = 20;
  for (size_t i = 0; i < len; i += CHUNK) {
    size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;
    uartTX.writeValue((const uint8_t*)s + i, n);
    BLE.poll();
  }
}

// ---- Event handlers for MAIN central ----
void onMainConnected(BLEDevice central) {
  mainConnected = true;
  mainConnectedAt = millis();
  // Seed characteristics so subscribers have an initial value:
  uartTX.writeValue((const uint8_t*)"ready\n", 6);
  forwardChar.writeValue((const uint8_t*)"0|0.00", 6);
  Serial.print("[RECV] MAIN connected: "); Serial.println(central.address());
}

void onMainDisconnected(BLEDevice central) {
  mainConnected = false;
  Serial.print("[RECV] MAIN disconnected: "); Serial.println(central.address());
}

// MAIN wrote "Devil Dog!" to us (or anything else): just print it
void onUartRXWritten(BLEDevice, BLECharacteristic) {
  uint8_t buf[24] = {0};
  int n = uartRX.readValue(buf, sizeof(buf));
  if (n > 0) {
    Serial.print("[RECV] From MAIN: ");
    Serial.write(buf, n);
    Serial.println();
  }
}

// ---- Debug helper if nRF UUIDs differ ----
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

  // Advertise forward service (others available post-connect)
  BLE.setAdvertisedService(forwardService);

  forwardService.addCharacteristic(forwardChar);
  uartService.addCharacteristic(uartTX);
  uartService.addCharacteristic(uartRX);

  BLE.addService(forwardService);
  BLE.addService(uartService);

  // Event handlers for MAIN connect/disconnect + RX writes
  BLE.setEventHandler(BLEConnected,    onMainConnected);
  BLE.setEventHandler(BLEDisconnected, onMainDisconnected);
  uartRX.setEventHandler(BLEWritten,   onUartRXWritten);

  forwardChar.writeValue((const uint8_t*)"0|0.00", 6);
  uartTX.writeValue((const uint8_t*)"ready\n", 6);

  BLE.advertise();               // advertise once; no periodic re-advertising
  Serial.println("[RECV] Advertising as GIGA_RECEIVER (Force+UART)...");
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
          Serial.println("[RECV] NRF subscribe failed; will use polling only.");
        } else {
          Serial.println("[RECV] Subscribed to NRF notifications.");
        }
      }

      nrfDev  = d;
      nrfChar = c;
      Serial.print("[RECV] Using NRF char "); Serial.println(nrfChar.uuid());

      lastPollMs  = millis();
      lastHelloMs = millis();
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
    Serial.println("[RECV] BLE.begin() failed");
    while (1) {}
  }

  setupPeripheral();
  connectToNRF();
}

void loop() {
  BLE.poll();

  // Reconnect to nRF if needed (don’t spam the stack)
  if (!nrfDev || !nrfDev.connected()) {
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt > 1000) {
      lastAttempt = millis();
      Serial.println("[RECV] NRF disconnected; reconnecting...");
      connectToNRF();
    }
    return;
  }

  unsigned long now = millis();

  // 1) Continuous force stream: actively poll nRF every 100 ms
  if (now - lastPollMs >= 100) {
    lastPollMs = now;

    uint8_t buf[48] = {0};
    int n = nrfChar.readValue(buf, sizeof(buf) - 1);  // poll read
    if (n > 0) {
      forwardChar.writeValue(buf, n);  // forward to MAIN (notify)
      Serial.print("[RECV] Force: ");
      Serial.println((char*)buf);
    }
  }

  // 2) Send "Hello MSTD Team" every 3 s — ONLY if MAIN is connected
  if (mainConnected && (now - mainConnectedAt) >= 1000) { // wait 1s after connect
    if (now - lastHelloMs >= 3000) {
      lastHelloMs = now;
      notifySmall("Hello MSTD Team");
      Serial.println("[RECV] Sent: Hello MSTD Team");
    }
  }

  delay(1);
}
