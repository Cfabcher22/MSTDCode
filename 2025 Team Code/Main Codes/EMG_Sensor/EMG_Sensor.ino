/*
  EMG Raw Signal Reader with Smoothing
  Board: Seeed XIAO nRF52840
  Input: Muscle Electrical Sensor Module (Analog Output)
  Output: Smoothed EMG signal printed to Serial Monitor and Plotter
*/

#define EMG_PIN 0           // Using pin D0 (also A0 on XIAO nRF52840)
const int BUFFER_SIZE = 10; // Moving average buffer size
int buffer[BUFFER_SIZE];
int bufferIndex = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(EMG_PIN, INPUT);

  for (int i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = 0;
  }

  Serial.println("EMG Monitoring Started...");
}

void sampleAndPrint() {
  int rawEMG = analogRead(EMG_PIN);

  buffer[bufferIndex] = rawEMG;
  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;

  int sum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sum += buffer[i];
  }
  int smoothedEMG = sum / BUFFER_SIZE;

  Serial.println(smoothedEMG);
}

void loop() {
  sampleAndPrint();  // Sample 1
  delay(500);        // Wait 0.5s

  sampleAndPrint();  // Sample 2
  delay(500);        // Wait 0.5s

  delay(500);        // Pause 0.5s before repeating
}
