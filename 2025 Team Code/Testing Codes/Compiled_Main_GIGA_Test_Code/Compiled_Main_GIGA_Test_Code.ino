/*
  GIGA R1 WiFi — MAIN (Central to GIGA_RECEIVER)

  Subscribes to:
    - Force Forward Char: b39a1002-0f3b-4b6c-a8ad-5c8471a40101 ("<ms>|<lbs>")
    - UART TX (Receiver -> Main): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
  Writes to:
    - UART RX (Main -> Receiver): 6e400002-b5a3-f393-e0a9-e50e24dcca9e

  Behavior:
    - Prints "ms,pounds" header then CSV lines as they arrive.
    - Whenever it receives "Hello MSTD Team" from Receiver, replies "Devil Dog!".
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

// --- Write small replies safely (<= 20 B per write) ---
void sendReplyToReceiver(const char* msg) {
  // keep under 20 B for default MTU; our string is tiny
  chUartRX.writeValue((const uint8_t*)msg, strlen(msg));
  BLE.poll();
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

  // CSV header once
  if (!headerPrinted) {
    Serial.println("ms,pounds");
    headerPrinted = true;
  }

  // Incoming force data (forwarded by Receiver)
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
        Serial.print("#WARN raw: "); Serial.println(buf);
      }
    }
  }

  // Incoming "Hello MSTD Team" from Receiver -> reply "Devil Dog!"
  if (chUartTX && chUartTX.valueUpdated()) {
    char inbuf[24] = {0};
    int n = chUartTX.readValue((uint8_t*)inbuf, sizeof(inbuf)-1);
    if (n > 0) {
      // Optional: print what we got
      Serial.print("[MAIN] Msg from RECV: ");
      Serial.println(inbuf);

      if (strncmp(inbuf, "Hello MSTD Team", 15) == 0) {
        sendReplyToReceiver("Devil Dog!");
        Serial.println("[MAIN] Replied: Devil Dog!");
      }
    }
  }

  delay(1);
}
