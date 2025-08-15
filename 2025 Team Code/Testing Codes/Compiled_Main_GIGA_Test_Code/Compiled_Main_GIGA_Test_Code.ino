/*
  GIGA R1 WiFi — MAIN
  Role: CENTRAL to "GIGA_RECEIVER"

  Subscribes to:
    - Force Forward Char: b39a1002-0f3b-4b6c-a8ad-5c8471a40101 (payload "<ms>|<lbs>")
    - UART TX (from Receiver): 6e400003-b5a3-f393-e0a9-e50e24dcca9e

  Writes to:
    - UART RX (to Receiver): 6e400002-b5a3-f393-e0a9-e50e24dcca9e

  Usage:
    - Open Serial Monitor (115200) on MAIN, type a line and press enter → sent to Receiver.
    - Chat from Receiver appears with "RECV>".
    - Force stream prints CSV as: ms,pounds
*/

#include <ArduinoBLE.h>

static const char* RECEIVER_NAME = "GIGA_RECEIVER";

// UUIDs (must match Receiver)
#define FORWARD_CHAR_UUID "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Notify from Receiver
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Write to Receiver

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
        Serial.println("[MAIN] Missing one or more required characteristics; rescanning...");
        d.disconnect();
        BLE.scanForName(RECEIVER_NAME);
        continue;
      }

      // Subscribe to notifications
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

  // Print CSV header once
  if (!headerPrinted) {
    Serial.println("ms,pounds");
    headerPrinted = true;
  }

  // Handle incoming force data
  if (chForce && chForce.valueUpdated()) {
    char buf[64] = {0};
    int n = chForce.readValue((uint8_t*)buf, sizeof(buf)-1);
    if (n > 0) {
      // Expect "<ms>|<lbs>"
      char* bar = strchr(buf, '|');
      if (bar) {
        *bar = '\0';
        unsigned long ms = strtoul(buf, NULL, 10);
        float pounds = atof(bar + 1);

        // CSV line
        Serial.print(ms);
        Serial.print(",");
        Serial.println(pounds, 2);
      } else {
        // If it's not delimited, just dump it
        Serial.print("#WARN raw: ");
        Serial.println(buf);
      }
    }
  }

  // Handle incoming chat from Receiver
  if (chUartTX && chUartTX.valueUpdated()) {
    uint8_t buf[201] = {0};
    int n = chUartTX.readValue(buf, sizeof(buf)-1);
    if (n > 0) {
      Serial.print("RECV> ");
      Serial.write(buf, n);
      Serial.println();
    }
  }

  // Read Main’s Serial → send to Receiver via UART RX
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuf.length()) {
        const char* msg = serialBuf.c_str();
        size_t len = serialBuf.length();
        if (len <= 200) chUartRX.writeValue((const uint8_t*)msg, len);
        serialBuf = "";
      }
    } else {
      if (serialBuf.length() < 200) serialBuf += c;
    }
  }

  delay(2);
}
