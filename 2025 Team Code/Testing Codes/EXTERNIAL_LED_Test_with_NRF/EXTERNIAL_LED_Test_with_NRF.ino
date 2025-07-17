const int batteryPin = A5;
const int redPin = D7;
const int greenPin = D8;
const int bluePin = D9;

void setup() {
  delay(100); // allow voltage to settle on battery power

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  digitalWrite(redPin, HIGH);
  digitalWrite(greenPin, HIGH);
  digitalWrite(bluePin, HIGH);

  // Serial.begin(9600); // Uncomment for testing with USB connected
}

void loop() {
  int raw = analogRead(batteryPin);
  float voltage = raw * (3.3 / 1023.0) * 2.0;  // Adjusted for 3.3V ADC and 2:1 voltage divider

  // Serial.print("Battery Voltage: ");  // Uncomment for testing
  // Serial.println(voltage);            // Uncomment for testing

  int batteryPercent = mapBatteryToPercent(voltage);
  showBatteryLED(batteryPercent);

  delay(2000); // Wait 2 seconds before next read
}

int mapBatteryToPercent(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)(((voltage - 3.0) / (4.2 - 3.0)) * 100);
}

void showBatteryLED(int percent) {
  if (percent > 75) {
    // Show green = good
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, HIGH);
  } else {
    // Show red = low
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, HIGH);
    digitalWrite(bluePin, HIGH);
  }
}
