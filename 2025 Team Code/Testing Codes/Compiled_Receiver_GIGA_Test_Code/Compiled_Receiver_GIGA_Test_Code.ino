/*
  GIGA R1 WiFi — RECEIVER
  Roles:
    - CENTRAL to NRF peripheral "NRF_FORCE_1" (subscribes to '<ms>|<lbs>')
    - PERIPHERAL to MAIN GIGA:
        * Service A (Force Forward):    b39a1001-0f3b-4b6c-a8ad-5c8471a40101
            - Char Forward (Notify/Read): b39a1002-0f3b-4b6c-a8ad-5c8471a40101
        * Service B (UART Chat):        6e400001-b5a3-f393-e0a9-e50e24dcca9e
            - TX to Main (Notify/Read):   6e400003-b5a3-f393-e0a9-e50e24dcca9e
            - RX from Main (Write/WoR):   6e400002-b5a3-f393-e0a9-e50e24dcca9e

  Usage:
    - Open Serial Monitor (115200) on Receiver. Type a line and press enter → sent to Main.
    - Any write from Main appears on Receiver’s serial.
    - Force packets from NRF are forwarded as-is to Main via Forward characteristic.
*/

#include <ArduinoBLE.h>

// ---------- Upstream (NRF) ----------
static const char* NRF_NAME = "NRF_FORCE_1";
#define NRF_CHAR_UUID "b39a0002-0f3b-4b6c-a8ad-5c8471a40001"  // "<ms>|<lbs>"

// ---------- Downstream (to Main) ----------
static const char* RECEIVER_NAME = "GIGA_RECEIVER";

// Force Forward service/char (Notify)
#define FORWARD_SERVICE_UUID "b39a1001-0f3b-4b6c-a8ad-5c8471a40101"
#define FORWARD_CHAR_UUID    "b39a1002-0f3b-4b6c-a8ad-5c8471a40101"

// UART chat service (Nordic-style UUIDs)
#define UART_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UART_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Notify to Main
#define UART_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Write from Main

// ---------- Peripheral objects (to Main) ----------
BLEService        forwardService(FORWARD_SERVICE_UUID);
BLECharacteristic forwardChar(FORWARD_CHAR_UUID, BLERead | BLENotify, 50);

BLEService        uartService(UART_SERVICE_UUID);
BLECharacteristic uartTX(UART_TX_UUID, BLERead | BLENotify, 200);
BLECharacteristic uartRX(UART_RX_UUID, BLEWriteWithoutResponse | BLEWrite, 200);

// ---------- Central handles (to NRF) ----------
BLEDevice         nrfDev;
BLECharacteristic nrfChar;

static const uint32_t SERIAL_BAUD = 115200;

void setupPeripheral() {
  BLE.setLocalName(RECEIVER_NAME);
  BLE.setDeviceName(RECEIVER_NAME);

  // Advertise both services
  BLE.setAdvertisedService(forwardService);
  BLE.setAdvertisedService(uartService);

  forwardService.addCharacteristic(forwardChar);
  uartService.addCharacteristic(uartTX);
  uartService.addCharacteristic(uartRX);

  BLE.addService(forwardService);
  BLE.addService(uartService);

  // Seed defaults
  forwardChar.writeValue((const uint8_t*)"0|0.0", 5);
  uartTX.writeValue((const uint8_t*)"RX ready", 8);

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
      BLECharacteristic c = d.characteristic(NRF_CHAR_UUID);
      if (!c) {
        Serial.println("[RECV] NRF char not found, rescanning...");
        d.disconnect();
        BLE.scan();
        continue;
      }
      if (!c.canSubscribe() || !c.subscribe()) {
        Serial.println("[RECV] NRF subscribe failed, rescanning...");
        d.disconnect();
        BLE.scan();
        continue;
      }

      nrfDev  = d;
      nrfChar = c;
      Serial.println("[RECV] Subscribed to NRF force stream.");
      return true;
    }
    BLE.poll();   // keep our peripheral role alive, too
    delay(5);
  }
}

// Send a text line from Receiver’s Serial to Main via UART TX (Notify)
void sendUARTToMain(const char* msg) {
  size_t len = strlen(msg);
  if (len > 0 && len <= 200) {
    uartTX.writeValue((const uint8_t*)msg, len); // notifies subscribers
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

String serialBuf;

void loop() {
  BLE.poll();

  // Keep advertising alive (idempotent)
  static uint32_t lastAdv = 0;
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
    uint8_t buf[51] = {0};
    int n = nrfChar.readValue(buf, sizeof(buf) - 1);
    if (n > 0) {
      forwardChar.writeValue(buf, n); // notifies Main subscribers
      Serial.print("[RECV] Force: ");
      Serial.println((char*)buf);
    }
  }

  // Handle chat from Main (writes to RX)
  if (uartRX.written()) {
    uint8_t inbuf[201] = {0};
    int n = uartRX.valueLength();
    if (n > 0 && n <= 200) {
      uartRX.readValue(inbuf, n);
      Serial.print("[RECV] From MAIN: ");
      Serial.write(inbuf, n);
      Serial.println();
    }
  }

  // Read Receiver’s Serial → send to Main via TX notify
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuf.length()) {
        sendUARTToMain(serialBuf.c_str());
        serialBuf = "";
      }
    } else {
      if (serialBuf.length() < 200) serialBuf += c;
    }
  }

  delay(2);
}
