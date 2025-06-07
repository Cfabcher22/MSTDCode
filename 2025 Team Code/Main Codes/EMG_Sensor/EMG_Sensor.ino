/*
  EMG Raw Signal Reader with Smoothing
  Board: Seeed XIAO nRF52840
  Input: Muscle Electrical Sensor Module (Analog Output)
  Output: Smoothed EMG signal printed to Serial Monitor and Plotter
*/

#define EMG_PIN A0         // EMG signal connected to Analog Pin A0 (D0 on XIAO nRF52840)
const int BUFFER_SIZE = 10; // Size for moving average smoothing
int buffer[BUFFER_SIZE];
int bufferIndex = 0;

void setup() {
  Serial.begin(115200);    // Initialize serial communication
  while (!Serial);         // Wait for Serial Monitor to open (important for nRF boards)

  pinMode(EMG_PIN, INPUT); // Set the EMG pin as input

  // Initialize buffer with zeros
  for (int i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = 0;
  }

  Serial.println("EMG Monitoring Started...");
}

void loop() {
  int rawEMG = analogRead(EMG_PIN); // Read raw analog value (0–1023 or 0–4095 based on board)
  
  // Store value in buffer for smoothing
  buffer[bufferIndex] = rawEMG;
  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;

  // Compute moving average
  int sum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sum += buffer[i];
  }
  int smoothedEMG = sum / BUFFER_SIZE;

  // Output the smoothed value
  Serial.println(smoothedEMG);

  delay(5); // Adjust delay for sampling rate (200 Hz here)
}
