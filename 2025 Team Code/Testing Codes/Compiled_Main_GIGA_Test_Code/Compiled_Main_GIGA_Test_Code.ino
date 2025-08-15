/*
  GIGA R1 WiFi — MAIN (Central to GIGA_RECEIVER)

  Subscribes to:
    - Force Forward: b39a1002-0f3b-4b6c-a8ad-5c8471a40101 ("<ms>|<lbs>")
    - UART TX (from Receiver): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
  Writes to:
    - UART RX (to Receiver): 6e400002-b5a3-f393-e0a9-e50e24dcca9e

  Serial (115200):
    - Prints "ms,pounds" header, then CSV lines.
    - Type a line + Enter to send chat to Receiver.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

static const char* RECEIVER_NAME = "GIGA_RECEIVER";

#define FORWARD_CHAR_UUID "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

BLEDevice         rxDev;
BLECharacteristic chForce;
BLECharacteristic chUartTX;
BLECharacteristic chUartRX;

bool headerPrinted = false;
static const uint32_t SERIAL_BAUD = 115200;

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
      BLECharacteristic t = d.characteristic(UART_TX_UUID);
      BLECharacteristic r = d.characteristic(UART_RX_UUID);

      if (!f || !t || !r) {
        Serial.println("[MAIN] Missing required characteristics; rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      bool ok = true;
      if (!f.canSubscribe() || !f.subscribe()) ok = false;
      if (!t.canSubscribe() || !t.subscribe()) ok = false;
      if (!ok) {
        Serial.println("[MAIN] subscribe() failed; rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      rxDev   = d;
      chForce = f;
      chUartTX= t;
      chUartRX= r;

      Serial.println("[MAIN] Connected & subscribed.");
      headerPrinted = false;
      return true;
    }
    BLE.poll();
    delay(5);
  }
}

// ---- UART write: 20-byte chunks ----
void writeUartRXChunked(const uint8_t* data, size_t len) {
  const size_t CHUNK = 20;
  for (size_t i = 0; i < len; i += CHUNK) {
    size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;
    chUartRX.writeValue(data + i, n);
    BLE.poll();
  }
}
void sendLineToReceiver(const char* msg) {
  char line[128];
  size_t m = strlen(msg);
  if (m > sizeof(line) - 2) m = sizeof(line) - 2;
  memcpy(line, msg, m);
  line[m++] = '\n';
  line[m] = 0;
  writeUartRXChunked((const uint8_t*)line, m);
}

String serialBuf;

void setup() {
  Serial.begin(SERIAL_BAUD);
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

  // CSV header once
  if (!headerPrinted) {
    Serial.println("ms,pounds");
    headerPrinted = true;
  }

  // Incoming force data
  if (chForce && chForce.valueUpdated()) {
    char buf[64] = {0};
    int n = chForce.readValue((uint8_t*)buf, sizeof(buf)-1);
    if (n > 0) {
      char* bar = strchr(buf, '|');
      if (bar) {
        *bar = '\0';
        unsigned long ms = strtoul(buf, NULL, 10);
        float pounds = atof(bar + 1);
        Serial.print(ms); Serial.print(","); Serial.println(pounds, 2);
      } else {
        Serial.print("#WARN raw: "); Serial.println(buf);
      }
    }
  }

  // Incoming chat from Receiver
  if (chUartTX && chUartTX.valueUpdated()) {
    uint8_t buf[64] = {0};
    int n = chUartTX.readValue(buf, sizeof(buf)-1);
    if (n > 0) {
      Serial.print("RECV> ");
      Serial.write(buf, n);
      Serial.println();
    }
  }

  // Read MAIN Serial → send to Receiver
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuf.length()) {
        sendLineToReceiver(serialBuf.c_str());
        serialBuf = "";
      }
    } else {
      if (serialBuf.length() < 120) serialBuf += c;
    }
  }

  delay(2);
}
