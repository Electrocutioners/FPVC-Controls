#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ---------------- Hall Sensor ----------------
const int hallPin = 2;

// If you use 1 magnet on the wheel, leave this as 1.
// If you add more magnets, change this.
const int pulsesPerRevolution = 1;

// These are updated inside the interrupt
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseMicros = 0;
volatile unsigned long pulseIntervalMicros = 0;

// ---------------- Pressure Sensor ----------------
const int pressurePin = A0;

// Full-scale pressure
const float pressureMaxPSI = 7500.0;

// Sensor full-scale voltage span assumption
// We will AUTO-ZERO the low end at startup.
const float sensorMaxVoltage = 4.5;

// This will be measured automatically at startup while system is at 0 psi
float sensorZeroVoltage = 0.5;

// ---------------- Timing ----------------
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 250;   // update screen 4x/sec

// If no pulse arrives for this long, show 0 mph
const unsigned long speedTimeoutMicros = 3000000UL; // 3 sec

// ---------------- Wheel ----------------
// Replace with your actual wheel circumference in meters
float wheelCircumference = 2.20;

// ---------------- Variables ----------------
float rpm = 0.0;
float speedMPH = 0.0;
float pressurePSI = 0.0;
float sensorVoltage = 0.0;

// ---------------- Hall Interrupt ----------------
void countPulse() {
  unsigned long now = micros();

  if (lastPulseMicros > 0) {
    pulseIntervalMicros = now - lastPulseMicros;
  }

  lastPulseMicros = now;
  pulseCount++;
}

// ---------------- Helper: read averaged ADC voltage ----------------
float readPressureVoltage(int samples = 20) {
  long sumADC = 0;

  for (int i = 0; i < samples; i++) {
    sumADC += analogRead(pressurePin);
    delay(2);
  }

  float rawADC = sumADC / (float)samples;
  return rawADC * (5.0 / 1023.0);
}

// ---------------- Helper: auto-zero pressure sensor ----------------
void calibratePressureZero() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Zeroing pressure");
  lcd.setCursor(0, 1);
  lcd.print("Keep at 0 psi...");

  float sumV = 0.0;
  const int runs = 40;

  for (int i = 0; i < runs; i++) {
    sumV += readPressureVoltage(10);
    delay(25);
  }

  sensorZeroVoltage = sumV / runs;

  lcd.setCursor(0, 2);
  lcd.print("Zero V: ");
  lcd.print(sensorZeroVoltage, 3);
  delay(1200);
  lcd.clear();
}

void setup() {
  lcd.init();
  lcd.backlight();
  Serial.begin(9600);

  lcd.setCursor(0, 0);
  lcd.print("Fluid Power Bike");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1000);

  pinMode(hallPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(hallPin), countPulse, FALLING);

  pinMode(pressurePin, INPUT);

  // Auto-zero pressure sensor at startup
  // IMPORTANT: make sure system is truly at 0 psi during startup
  calibratePressureZero();

  lastDisplayUpdate = millis();
}

void loop() {
  unsigned long nowMillis = millis();

  if (nowMillis - lastDisplayUpdate >= displayInterval) {
    // ---------- Copy hall data safely ----------
    noInterrupts();
    unsigned long countCopy = pulseCount;
    unsigned long intervalCopy = pulseIntervalMicros;
    unsigned long lastPulseCopy = lastPulseMicros;
    interrupts();

    // ---------- Compute speed in MPH ----------
    // Using time between pulses gives much better low-speed resolution.
    if (lastPulseCopy == 0 || (micros() - lastPulseCopy) > speedTimeoutMicros || intervalCopy == 0) {
      speedMPH = 0.0;
      rpm = 0.0;
    } else {
      float revPerSecond = 1000000.0 / intervalCopy;
      revPerSecond /= pulsesPerRevolution;

      float speedMPS = revPerSecond * wheelCircumference;
      speedMPH = speedMPS * 2.23694;   // yes, MPH
      rpm = revPerSecond * 60.0;
    }

    // ---------- Read pressure ----------
    sensorVoltage = readPressureVoltage(20);

    // Convert voltage to pressure using auto-zeroed low point
    pressurePSI = ((sensorVoltage - sensorZeroVoltage) * pressureMaxPSI) /
                  (sensorMaxVoltage - sensorZeroVoltage);

    if (pressurePSI < 0) pressurePSI = 0;
    if (pressurePSI > pressureMaxPSI) pressurePSI = pressureMaxPSI;

    // ---------- LCD ----------
    lcd.setCursor(0, 0);
    lcd.print("RPM:");
    lcd.print(rpm, 0);
    lcd.print("        ");

    lcd.setCursor(0, 1);
    lcd.print("Speed:");
    lcd.print(speedMPH, 2);
    lcd.print(" mph   ");

    lcd.setCursor(0, 2);
    lcd.print("Press:");
    lcd.print(pressurePSI, 0);
    lcd.print(" psi   ");

    lcd.setCursor(0, 3);
    lcd.print("V:");
    lcd.print(sensorVoltage, 3);
    lcd.print(" C:");
    lcd.print(countCopy);
    lcd.print("   ");

    // ---------- Serial ----------
    Serial.print("RPM: ");
    Serial.print(rpm, 2);
    Serial.print(" | Speed: ");
    Serial.print(speedMPH, 2);
    Serial.print(" mph");
    Serial.print(" | Pressure: ");
    Serial.print(pressurePSI, 1);
    Serial.print(" psi");
    Serial.print(" | Voltage: ");
    Serial.print(sensorVoltage, 3);
    Serial.print(" V");
    Serial.print(" | ZeroV: ");
    Serial.println(sensorZeroVoltage, 3);

    lastDisplayUpdate = nowMillis;
  }
}