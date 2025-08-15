/*
  GIGA R1 WiFi — RECEIVER (Fixed for ArduinoBLE variants)
  Roles:
    - CENTRAL to NRF peripheral "NRF_FORCE_1": subscribes to 180C/2A56 (payload "<ms>|<lbs>")
    - PERIPHERAL to MAIN:
        * Force Forward service: b39a1001-0f3b-4b6c-a8ad-5c8471a40101
            - Char (Notify/Read): b39a1002-0f3b-4b6c-a8ad-5c8471a40101
        * UART Chat (Nordic NUS):
            - Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
            - TX (Notify/Read): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
            - RX (Write/WoResp): 6e400002-b5a3-f393-e0a9-e50e24dcca9e

  Serial (115200):
    - Type a line + Enter to send chat to MAIN.
    - Prints incoming chat and forwarded force strings.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

// ---------- Upstream (NRF) ----------
static const char* NRF_NAME = "NRF_FORCE_1";
#define NRF_SERVICE_UUID_STD "180C"
#define NRF_CHAR_UUID_STD    "2A56"

// ---------- Downstream (to Main) ----------
static const char* RECEIVER_NAME = "GIGA_RECEIVER";

// Force Forward
#define FORWARD_SERVICE_UUID "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID    "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

// UART chat (NUS)
#define UART_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Notify to Main
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Write from Main

// ---------- Peripheral objects (to Main) ----------
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 64);

BLEService        uartService(UART_SERVICE_UUID);
BLECharacteristic uartTX(UART_TX_UUID, BLERead | BLENotify, 64);
BLECharacteristic uartRX(UART_RX_UUID, BLEWriteWithoutResponse | BLEWrite, 64);

// ---------- Central handles (to NRF) ----------
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

static const uint32_t SERIAL_BAUD = 115200;

// ---- Utility: enumerate services/chars (compatible with older ArduinoBLE) ----
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
      // Note: some ArduinoBLE builds lack canWriteWithoutResponse(); omitted for compatibility.
      Serial.println();
    }
  }
}

bool resolveNRFCharacteristic(BLEDevice& d, BLECharacteristic& out) {
  BLEService svcStd = d.service(NRF_SERVICE_UUID_STD);
  if (svcStd) {
    BLECharacteristic c2 = svcStd.characteristic(NRF_CHAR_UUID_STD);
    if (c2) { out = c2; return true; }
    // fallback: any notifiable char in 180C
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

  // It's fine to advertise a single service; both are available after connect
  BLE.setAdvertisedService(forwardService);

  forwardService.addCharacteristic(forwardChar);
  uartService.addCharacteristic(uartTX);
  uartService.addCharacteristic(uartRX);

  BLE.addService(forwardService);
  BLE.addService(uartService);

  forwardChar.writeValue((const uint8_t*)"0|0.0", 5);
  uartTX.writeValue((const uint8_t*)"RX ready\n", 9);

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
      if (!c.canSubscribe() || !c.subscribe()) {
        Serial.println("[RECV] NRF subscribe failed; rescanning...");
        d.disconnect();
        BLE.scan();
        continue;
      }

      nrfDev  = d;
      nrfChar = c;
      Serial.print("[RECV] Subscribed to NRF char "); Serial.println(nrfChar.uuid());
      return true;
    }
    BLE.poll();   // keep our peripheral role alive
    delay(5);
  }
}

// ---- UART helpers: 20-byte chunks (safe for default MTU) ----
void uartNotifyToMain(const uint8_t* data, size_t len) {
  const size_t CHUNK = 20;
  for (size_t i = 0; i < len; i += CHUNK) {
    size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;
    uartTX.writeValue(data + i, n);
    BLE.poll();
  }
}
void uartLineToMain(const char* msg) {
  char line[128];
  size_t m = strlen(msg);
  if (m > sizeof(line) - 2) m = sizeof(line) - 2;
  memcpy(line, msg, m);
  line[m++] = '\n';
  line[m] = 0;
  uartNotifyToMain((const uint8_t*)line, m);
}

String serialBuf;

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

  // Keep advertising alive
  static unsigned long lastAdv = 0;
  if (millis() - lastAdv > 4000) {
    BLE.advertise();
    lastAdv = millis();
  }

  // Reconnect to NRF if needed
  if (!nrfDev || !nrfDev.connected()) {
    Serial.println("[RECV] NRF disconnected; reconnecting...");
    connectToNRF();
    return;
  }

  // Forward force packets from NRF to Main
  if (nrfChar && nrfChar.valueUpdated()) {
    uint8_t buf[64] = {0};
    int n = nrfChar.readValue(buf, sizeof(buf) - 1);
    if (n > 0) {
      forwardChar.writeValue(buf, n); // notifies Main
      Serial.print("[RECV] Force: ");
      Serial.println((char*)buf);
    }
  }

  // Chat: handle text from Main (writes to RX)
  if (uartRX.written()) {
    uint8_t inbuf[64] = {0};
    int n = uartRX.readValue(inbuf, sizeof(inbuf));
    if (n > 0) {
      Serial.print("[RECV] From MAIN: ");
      Serial.write(inbuf, n);
      Serial.println();
    }
  }

  // Read Receiver’s Serial → send to Main (newline-terminated)
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuf.length()) {
        uartLineToMain(serialBuf.c_str());
        serialBuf = "";
      }
    } else {
      if (serialBuf.length() < 120) serialBuf += c;
    }
  }

  delay(2);
}
