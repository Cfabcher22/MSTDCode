const int emgPin = A0;

void setup() {
  Serial.begin(115200);
  while (!Serial);
}

void loop() {
  int raw = analogRead(emgPin);
  Serial.println(raw);
  delay(300); // keep fast enough for visualization
}
