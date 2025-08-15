/*
  GIGA R1 WiFi — RECEIVER
  - CENTRAL to nRF "NRF_FORCE_1" (service 0x180C, char 0x2A56 "<ms>|<lbs>")
      * Subscribes AND actively polls every ~100 ms (reliable continuous stream)
  - PERIPHERAL to MAIN:
      * Force Forward service: b39a1001-0f3b-4b6c-a8ad-5c8471a40101
          - Forward Char (Notify/Read): b39a1002-0f3b-4b6c-a8ad-5c8471a40101
      * Simple “ping-pong” messaging (Nordic NUS-like):
          - Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
          - TX Notify (Receiver -> Main): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
          - RX Write  (Main -> Receiver): 6e400002-b5a3-f393-e0a9-e50e24dcca9e
  - Every 3 seconds, sends "Hello MSTD Team" to Main via TX notify.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

// ---------- Upstream (nRF) ----------
static const char* NRF_NAME = "NRF_FORCE_1";
#define NRF_SERVICE_UUID_STD "180C"
#define NRF_CHAR_UUID_STD    "2A56"

// ---------- Downstream (to Main) ----------
static const char* RECEIVER_NAME = "GIGA_RECEIVER";

#define FORWARD_SERVICE_UUID "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID    "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

#define UART_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Notify (Receiver -> Main)
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Write  (Main -> Receiver)

// ---------- Peripheral (to Main) ----------
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 48);

BLEService        uartService(UART_SERVICE_UUID);
BLECharacteristic uartTX(UART_TX_UUID, BLERead | BLENotify, 24);
BLECharacteristic uartRX(UART_RX_UUID, BLEWrite | BLEWriteWithoutResponse, 24);

// ---------- Central handles (to nRF) ----------
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

static const uint32_t SERIAL_BAUD = 115200;

// --- Timers ---
unsigned long lastPollMs   = 0;   // nRF read poll (force data)
unsigned long lastHelloMs  = 0;   // periodic "Hello MSTD Team"

// --- Helpers ---
void uartNotifyToMain(const uint8_t* data, size_t len) {
  // keep chunks <= 20 bytes for default MTU safety
  const size_t CHUNK = 20;
  for (size_t i = 0; i < len; i += CHUNK) {
    size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;
    uartTX.writeValue(data + i, n);
    BLE.poll();
  }
}
void sendHelloToMain() {
  const char* msg = "Hello MSTD Team";
  uartNotifyToMain((const uint8_t*)msg, strlen(msg));
}

void onUartRXWritten(BLEDevice, BLECharacteristic) {
  // Main responded back — print for visibility
  uint8_t inbuf[24] = {0};
  int n = uartRX.readValue(inbuf, sizeof(inbuf));
  if (n > 0) {
    Serial.print("[RECV] Msg from MAIN: ");
    Serial.write(inbuf, n);
    Serial.println();
  }
}

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

  BLE.setAdvertisedService(forwardService); // advertise one (both available after connect)

  forwardService.addCharacteristic(forwardChar);
  uartService.addCharacteristic(uartTX);
  uartService.addCharacteristic(uartRX);

  BLE.addService(forwardService);
  BLE.addService(uartService);

  forwardChar.writeValue((const uint8_t*)"0|0.00", 6);
  uartTX.writeValue((const uint8_t*)"ready\n", 6);

  uartRX.setEventHandler(BLEWritten, onUartRXWritten);

  BLE.advertise();
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

      // Try to subscribe (optional now that we poll, but keep it enabled)
      if (c.canSubscribe()) {
        if (!c.subscribe()) {
          Serial.println("[RECV] NRF subscribe failed; continuing with polling.");
        } else {
          Serial.println("[RECV] Subscribed to NRF notifications.");
        }
      }

      nrfDev  = d;
      nrfChar = c;
      Serial.print("[RECV] Using NRF char "); Serial.println(nrfChar.uuid());

      // Reset timers
      lastPollMs = lastHelloMs = millis();
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

  // Keep advertising for Main (safe if called periodically)
  static unsigned long lastAdv = 0;
  if (millis() - lastAdv > 4000) {
    BLE.advertise();
    lastAdv = millis();
  }

  // Reconnect to nRF if needed
  if (!nrfDev || !nrfDev.connected()) {
    Serial.println("[RECV] NRF disconnected; reconnecting...");
    connectToNRF();
    return;
  }

  // 1) CONTINUOUS FORCE STREAM (active poll every ~100 ms)
  unsigned long now = millis();
  if (now - lastPollMs >= 100) {
    lastPollMs = now;

    uint8_t buf[48] = {0};
    int n = nrfChar.readValue(buf, sizeof(buf) - 1);  // actively read
    if (n > 0) {
      // Forward to Main immediately (Notify)
      forwardChar.writeValue(buf, n);

      // Also print locally for verification
      Serial.print("[RECV] Force: ");
      Serial.println((char*)buf);
    }
  }

  // 2) PERIODIC “HELLO MSTD TEAM” every 3 seconds to Main
  if (now - lastHelloMs >= 3000) {
    lastHelloMs = now;
    sendHelloToMain();
    Serial.println("[RECV] Sent: Hello MSTD Team");
  }

  delay(1);
}
