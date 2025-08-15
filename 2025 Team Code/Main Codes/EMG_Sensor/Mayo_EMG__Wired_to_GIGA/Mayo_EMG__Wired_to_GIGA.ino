#define EMG_PIN A0

void setup() {
  Serial.begin(115200);
  pinMode(EMG_PIN, INPUT);
}

void loop() {
  int emgValue = analogRead(EMG_PIN);
  Serial.println(emgValue);  // Print raw ADC value (0–1023)
  delay(50);  // Fast enough to catch spikes
}
