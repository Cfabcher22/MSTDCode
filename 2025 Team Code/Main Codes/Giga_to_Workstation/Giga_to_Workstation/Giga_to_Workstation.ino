#include <ArduinoBLE.h>  // Include BLE support for Arduino

// Define a custom UART-like BLE service UUID
BLEService uartService("19B10000-E8F2-537E-4F6C-D104768A1214");

// Define a characteristic for sending messages to the central device (like a PC)
// BLERead allows the client to read the last message
// BLENotify allows the client to receive real-time updates
BLECharacteristic txChar("19B10001-E8F2-537E-4F6C-D104768A1214", 
                         BLERead | BLENotify, 20);  // 20 bytes max for BLE

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // Use built-in LED as a BLE status indicator
  Serial.begin(115200);          // Open serial port for debugging
  while (!Serial);               // Wait for Serial Monitor to connect (optional)

  // Initialize BLE stack
  if (!BLE.begin()) {
    Serial.println("BLE initialization failed!");
    while (1);  // Stop execution if BLE can't start
  }

  // Set the advertised name of the BLE device
  BLE.setLocalName("GIGA_BLE_UART");

  // Register the custom service and characteristic
  BLE.setAdvertisedService(uartService);
  uartService.addCharacteristic(txChar);
  BLE.addService(uartService);

  // Start advertising BLE to make the device discoverable
  BLE.advertise();
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED to indicate advertising

  Serial.println("BLE UART service started and advertising.");
}

void loop() {
  // Check if a BLE central device (like a PC) connects
  BLEDevice central = BLE.central();
  BLE.poll();  // Always keep BLE stack running smoothly

  if (central) {
    // Connection successful
    Serial.print("Connected to central: ");
    Serial.println(central.address());

    // Send messages while central is connected
    while (central.connected()) {
      txChar.writeValue("Hello from GIGA!\n");  // Send message over BLE
      BLE.poll();                               // Maintain BLE connection
      delay(1000);                              // Wait 1 second before next message
    }

    // When central disconnects
    Serial.println("Disconnected from central.");
  }
}
