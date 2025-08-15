
const int batteryPin = A5;
const int redPin = D7;
const int greenPin = D8;
const int bluePin = D9;

void setup() {
  Serial.begin(9600);

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  digitalWrite(redPin, HIGH);
  digitalWrite(greenPin, HIGH);
  digitalWrite(bluePin, HIGH);
}

void loop() {
  int raw = analogRead(batteryPin);
  float voltage = raw * (5.0 / 1023.0) * 2.0;  // 2.0 for voltage divider

  Serial.print("Battery Voltage: ");
  Serial.println(voltage);

  int batteryPercent = mapBatteryToPercent(voltage);

  // Display LED color based on battery percentage
  showBatteryLED(batteryPercent);

  delay(2000); // Read every 2 seconds
}

int mapBatteryToPercent(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)(((voltage - 3.0) / (4.2 - 3.0)) * 100);
}

void showBatteryLED(int percent) {
  if (percent > 75) {
    // Green = low
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, HIGH);
  } else {
    // Red = low
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, HIGH);
    digitalWrite(bluePin, HIGH);
  }
}