#include <ArduinoBLE.h>
//BLE Advertising (blue Blinking)
//BLE Connected (Solid BLUE)
//Battery Voltage reading via voltage divider
//Battery percentage display (green > 80%, red low (blinking) < 20%, red < 5% when dead )
//Battery CHarging Indicator (green blinking)
//Control of both external and onboard RGB LEDs for testing

//want intial led blink status

//Battery voltage pin
#define BATTERY_PIN A0

// ==== External LED pins ====
#define EXT_RED_PIN 7
#define EXT_GREEN_PIN 8
#define EXT_BLUE_PIN 9

// ==== Onboard LED pins ====
#define ONB_RED_PIN 12
#define ONB_GREEN_PIN 13
#define ONB_BLUE_PIN 14

const float CHARGING_VOLTAGE = 4.15;
const unsigned long blinkInterval = 500;

//Variables
unsigned long previousMillis = 0;
bool ledBlinkState = false;

//vairbales
bool batteryLEDShown = false;
bool lowBatteryLEDShown = false;
bool deadBatteryLEDShown = false;
unsigned long blinkStartMillis = 0;
bool blinkInitialStatus = true;
unsigned long lastSerialPrint = 0;

// ==== LED Control ====
void setLEDs(bool redOn, bool greenOn, bool blueOn) {
  digitalWrite(EXT_RED_PIN, redOn ? LOW : HIGH);
  digitalWrite(EXT_GREEN_PIN, greenOn ? LOW : HIGH);
  digitalWrite(EXT_BLUE_PIN, blueOn ? LOW : HIGH);

  digitalWrite(ONB_RED_PIN, redOn ? LOW : HIGH);
  digitalWrite(ONB_GREEN_PIN, greenOn ? LOW : HIGH);
  digitalWrite(ONB_BLUE_PIN, blueOn ? LOW : HIGH);
}

// ==== Function Prototypes ====
void setLEDs(bool redOn, bool greenOn, bool blueOn);
float readBatteryVoltage();
int batteryPercent(float voltage);
void showBatteryLED(int percent, bool charging);

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(EXT_RED_PIN, OUTPUT);
  pinMode(EXT_GREEN_PIN, OUTPUT);
  pinMode(EXT_BLUE_PIN, OUTPUT);

  pinMode(ONB_RED_PIN, OUTPUT);
  pinMode(ONB_GREEN_PIN, OUTPUT);
  pinMode(ONB_BLUE_PIN, OUTPUT);

  setLEDs(false, false, false);  // all LEDS off

  // Start BLE
  if (!BLE.begin()) {
    Serial.println("BLE init failed");
    while (1)
      ;
  }

  BLE.setLocalName("Battery+BLE Unit");
  BLE.advertise();
}

// ==== Main Loop ====
void loop() {
  BLE.poll();
  BLEDevice central = BLE.central();

  float voltage = readBatteryVoltage();
  int percent = batteryPercent(voltage);
  bool isCharging = voltage > CHARGING_VOLTAGE;

  // Start initial LED blink to show battery status
  if (blinkInitialStatus) {
    unsigned long currentMillis = millis();
    if (currentMillis - blinkStartMillis < 3000) {  // Blink for 3 seconds after start
      // Blink the LED based on battery percentage
      if (percent > 80) {
        setLEDs(false, true, false);  // Green (Battery > 80%)
      } else if (percent <= 20 && percent > 5) {
        setLEDs(true, false, false);  // Red (Battery < 20%)
        delay(250);
        setLEDs(false, false, false);
        delay(250);
      } else if (percent <= 5) {
        setLEDs(true, false, false);  // Red (Battery < 5%)
      }
    } else {
      blinkInitialStatus = false;    // Stop blinking after 3 seconds
      setLEDs(false, false, false);  // Turn off LEDs after blink
    }
  }

  // BLE disconnected (blink blue)
  if (!central) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= blinkInterval) {
      previousMillis = currentMillis;
      ledBlinkState = !ledBlinkState;
      if (ledBlinkState)
        setLEDs(false, false, true);  // blue ON
      else
        setLEDs(false, false, false);  // OFF
    }
  }

  // BLE connected (solid blue)
  if (central && central.connected()) {
    setLEDs(false, false, true);  // solid blue
    delay(1000);                  // Short delay to keep LED responsive

    // Show the battery LED only once at specific battery percentages
    if (!batteryLEDShown) {
      showBatteryLED(percent, isCharging);
      batteryLEDShown = true;  // Prevent LED from turning on repeatedly
    }

    // Handle low battery conditions
    if (percent <= 20 && !lowBatteryLEDShown) {
      showBatteryLED(percent, isCharging);
      lowBatteryLEDShown = true;
    }

    // Handle dead battery conditions
    if (percent <= 5 && !deadBatteryLEDShown) {
      showBatteryLED(percent, isCharging);
      deadBatteryLEDShown = true;
    }

    delay(1000);
  }


  if (millis() - lastSerialPrint >= 10000) {  // Print once per second
    lastSerialPrint = millis();
    Serial.print("Voltage: ");
    Serial.print(voltage);
    Serial.print(" V (");
    Serial.print(percent);
    Serial.println("%)");
  }
}

// ==== Battery Reading ====
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return raw * (3.3 / 1023.0) * 2.0;  // 2:1 voltage divider
}

int batteryPercent(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)(((voltage - 3.0) / (4.2 - 3.0)) * 100);
}

// ==== LED Status Battery ====
void showBatteryLED(int percent, bool charging) {
  if (charging) {
    // Blink green if charging
    setLEDs(false, true, false);
    delay(250);
    setLEDs(false, false, false);
    delay(250);
  } else if (percent > 80) {
    setLEDs(false, true, false);  // green
  } else if (percent <= 20) {
    // Red blinking if dead
    setLEDs(true, false, false);
    delay(250);
    setLEDs(false, false, false);
    delay(250);
  } else if (percent <= 5) {
    setLEDs(true, false, false);  // solid red
  }
}
