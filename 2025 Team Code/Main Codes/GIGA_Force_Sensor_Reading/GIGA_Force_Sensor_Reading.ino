#include <ArduinoBLE.h>  // Include the Arduino BLE library

// Define custom BLE UART service and TX characteristic UUIDs
BLEService uartService("19B10000-E8F2-537E-4F6C-D104768A1214");  // Custom UART service
BLECharacteristic txChar("19B10001-E8F2-537E-4F6C-D104768A1214", 
                         BLERead | BLENotify, 20);  // Notify + Read = GIGA → PC

void setup() {
  // Initialize BLE hardware
  if (!BLE.begin()) {
    // If BLE fails to start, freeze (useful for debug with Serial if needed)
    while (1);
  }

  // Set advertised name for the GIGA (seen by your PC or scanner)
  BLE.setLocalName("GIGA_BLE_UART");

  // Add the custom service to the advertised BLE profile
  BLE.setAdvertisedService(uartService);

  // Add the TX characteristic to the UART service
  uartService.addCharacteristic(txChar);

  // Add the complete service to the BLE stack
  BLE.addService(uartService);

  // Start BLE advertising — GIGA is now visible to BLE central devices (like your PC)
  BLE.advertise();
}

void loop() {
  // Wait for a central device (your PC) to connect
  BLEDevice central = BLE.central();

  // If a central device connects:
  if (central) {
    // While the central is connected, transmit text every 1 second
    while (central.connected()) {
      txChar.writeValue("Hello from GIGA!\n");  // Send message
      delay(1000);                              // Wait 1 second
    }

    // After disconnect, go back to advertising automatically
  }
}
