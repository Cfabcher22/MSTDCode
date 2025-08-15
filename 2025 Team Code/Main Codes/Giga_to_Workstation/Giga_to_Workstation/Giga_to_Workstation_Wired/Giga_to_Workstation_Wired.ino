/*
  GIGA to PC USB Serial Communication
  Sends "Hello from GIGA via USB!" every second
*/

void setup() {
  Serial.begin(115200);      // Start USB Serial at 115200 baud
  while (!Serial);           // Wait for serial connection to PC
  Serial.println("GIGA Ready");
}

void loop() {
  Serial.println("Hello from GIGA via USB!");
  delay(1000);               // Wait 1 second
}
