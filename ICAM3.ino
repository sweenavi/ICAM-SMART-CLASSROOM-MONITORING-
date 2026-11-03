#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <math.h>

// ==================================================
// ICAM - INTELLIGENT CLASSROOM AUTOMATION & MONITORING
// ESP32-S3 | Non-blocking millis() implementation
// ==================================================

// ---------- GPIO CONFIGURATION ----------
#define SDA_PIN       8
#define SCL_PIN       9

#define DHT_PIN       4
#define DHT_TYPE      DHT22

#define LDR_PIN       6
#define MQ2_PIN       1

#define BUZZER_PIN    2

#define FAN_RELAY     15   // Relay 1: Fan
#define LED_RELAY     16   // Relay 2: LED strip

#define IR1_PIN       18
#define IR2_PIN       17
#define ACS_PIN       10   // ACS712 5A analog output

// ---------- SETTINGS ----------
const unsigned long DHT_INTERVAL    = 2500UL;
const unsigned long SENSOR_INTERVAL = 200UL;
const unsigned long OLED_INTERVAL   = 500UL;
const unsigned long ACS_INTERVAL    = 500UL;

// ACS712 5A settings
const float ACS_SENSITIVITY = 0.185f; // 185 mV/A
const int ACS_SAMPLES = 100;

const float TEMP_THRESHOLD = 33.0;
const int GAS_THRESHOLD = 2000;

// Active-LOW relay configuration
const int RELAY_ON  = LOW;
const int RELAY_OFF = HIGH;

// Buzzer: active-HIGH
const int BUZZER_ON  = HIGH;
const int BUZZER_OFF = LOW;

// IR sensors: active-LOW, as in reference code
const int IR_DETECTED = LOW;

// Student counter settings
const int OCCUPIED_THRESHOLD = 2;
const unsigned long IR_SEQUENCE_TIMEOUT = 1000UL;
const unsigned long IR_CLEAR_TIMEOUT = 100UL;

// Buzzer durations: classroom status = 3 seconds, gas alert = 5 seconds
const unsigned long CLASS_BUZZER_DURATION = 1500UL;
const unsigned long GAS_BUZZER_DURATION   = 5000UL;

// ---------- OBJECTS ----------
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SH1106G display(128, 64, &Wire, -1);

// ---------- SENSOR VARIABLES ----------
float temperature = 0.0;
float humidity = 0.0;

int ldrRaw = 0;
int mq2Raw = 0;
float mq2AdcVoltage = 0.0;

float acsZeroVoltage = 0.0f;
float acsVoltage = 0.0f;
float currentAmps = 0.0f;
float currentMilliAmps = 0.0f;

bool dhtOK = false;

bool previousClassOccupied = false;
bool classStateInitialized = false;

// ---------- AUTOMATION STATES ----------
bool fanOn = false;
bool lightOn = false;
bool gasUnsafe = false;

// LDR interpretation:
const int LDR_LIGHT_DETECTED = LOW;

// ---------- STUDENT COUNTING ----------
int studentCount = 0;

enum IRSequence {
  IR_IDLE,
  IR1_FIRST,
  IR2_FIRST
};

IRSequence irSequence = IR_IDLE;

unsigned long irSequenceStart = 0;
unsigned long lastIrClear = 0;

bool previousIr1 = false;
bool previousIr2 = false;

// ---------- NON-BLOCKING TIMERS ----------
unsigned long lastDhtRead = 0;
unsigned long lastSensorRead = 0;
unsigned long lastOledUpdate = 0;
unsigned long lastAcsRead = 0;
unsigned long activeBuzzerDuration = 0;

// ---------- BUZZER STATE MACHINE ----------
bool buzzerActive = false;
bool buzzerOutputOn = false;

int beepCount = 0;
unsigned long buzzerTimer = 0;

bool previousGasUnsafe = false;
unsigned long lastSerialStatus = 0;
#include "icam_web.h"
// ==================================================
// SETUP
// ==================================================

void setup() {
  Serial.begin(115200);
  // Give the ESP32-S3 native USB Serial Monitor time to connect.
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < 5000UL)) {
    delay(10);
  }
  Serial.println();
  Serial.println("ICAM V2 booting... Serial baud: 115200");

  pinMode(LDR_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

  pinMode(FAN_RELAY, OUTPUT);
  pinMode(LED_RELAY, OUTPUT);

  // Both relays OFF at startup
  digitalWrite(FAN_RELAY, RELAY_OFF);
  digitalWrite(LED_RELAY, RELAY_OFF);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_PIN, ADC_11db);
  pinMode(ACS_PIN, INPUT);
  analogSetPinAttenuation(ACS_PIN, ADC_11db);
 

  dht.begin();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (!display.begin(0x3C, true)) {
    Serial.println("OLED initialization failed!");
    while (true) {
      // Stop if display initialization fails
    }
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ICAM SYSTEM");
  display.println("Initializing...");
  display.display();

  // Calibrate ACS712 with fan and LED relays OFF.
  // Assumes ACS712 OUT is connected directly to GPIO 10.
  Serial.println("ACS712 calibration: keep loads OFF...");
  unsigned long acsTotalMv = 0;
  for (int i = 0; i < 500; i++) {
    acsTotalMv += analogReadMilliVolts(ACS_PIN);
    delayMicroseconds(500);
  }
  acsZeroVoltage = (acsTotalMv / 500.0f) / 1000.0f;
  Serial.print("ACS712 zero voltage: ");
  Serial.print(acsZeroVoltage, 3);
  Serial.println(" V");

  unsigned long now = millis();

  // Trigger first sensor readings without blocking
  lastDhtRead = now - DHT_INTERVAL;
  lastSensorRead = now - SENSOR_INTERVAL;
  lastOledUpdate = now - OLED_INTERVAL;

  Serial.println("ICAM system started");
icamWebBegin();

}

// ==================================================
// MAIN LOOP
// ==================================================

void loop() {
  unsigned long now = millis();

  readDHT(now);
  readOtherSensors(now);
  readACS712(now);
  controlFan();
  controlLight();
  monitorGas(now);
  updateBuzzer(now);
  updateStudentCounter(now);
  checkClassroomStatus(now);
  updateOLED(now);
  icamWebLoop();
  printSerialStatus(now);
}

// ==================================================
// SERIAL MONITOR STATUS OUTPUT
// ==================================================
void printSerialStatus(unsigned long now) {
  if (now - lastSerialStatus < 2500UL) return;
  lastSerialStatus = now;

  Serial.println("---------------- ICAM SENSOR STATUS ----------------");
  Serial.print("Temperature: ");
  if (dhtOK) Serial.print(temperature, 1); else Serial.print("READ ERROR");
  Serial.print(" C | Humidity: ");
  if (dhtOK) Serial.print(humidity, 1); else Serial.print("READ ERROR");
  Serial.println(" %");

  Serial.print("LDR raw (digital): "); Serial.print(ldrRaw);
  Serial.print(" | Ambient light: "); Serial.println(lightOn ? "DETECTED" : "NOT DETECTED");
  Serial.print("MQ-2 raw ADC: "); Serial.print(mq2Raw);
  Serial.print(" | ADC estimate: "); Serial.print(mq2AdcVoltage, 2);
  Serial.print(" V | Gas status: "); Serial.println(gasUnsafe ? "UNSAFE" : "SAFE");
  Serial.print("Current: "); Serial.print(currentAmps, 3);
  Serial.print(" A | "); Serial.print(currentMilliAmps, 0); Serial.println(" mA");
  Serial.print("Student count: "); Serial.print(studentCount);
  Serial.print(" | Classroom: "); Serial.println(studentCount >= OCCUPIED_THRESHOLD ? "OCCUPIED" : "EMPTY");
  Serial.print("Fan: "); Serial.print(fanOn ? "ON" : "OFF");
  Serial.print(" | LED relay: "); Serial.print(digitalRead(LED_RELAY) == RELAY_ON ? "ON" : "OFF");
  Serial.print(" | Buzzer: "); Serial.println(digitalRead(BUZZER_PIN) == BUZZER_ON ? "ON" : "OFF");
}

// ==================================================
// DHT22: TEMPERATURE AND HUMIDITY
// ==================================================

void readDHT(unsigned long now) {
  if (now - lastDhtRead < DHT_INTERVAL) {
    return;
  }

  lastDhtRead = now;

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity = h;
    dhtOK = true;
  } else {
    dhtOK = false;
    Serial.println("DHT22 read error");
  }
}

// ==================================================
// LDR AND MQ-2 READINGS
// ==================================================


void readOtherSensors(unsigned long now) {
  if (now - lastSensorRead < SENSOR_INTERVAL) {
    return;
  }

  lastSensorRead = now;

  // LDR digital output: LOW or HIGH
  ldrRaw = digitalRead(LDR_PIN);

  // MQ-2 analog output
  mq2Raw = analogRead(MQ2_PIN);
  mq2AdcVoltage = (mq2Raw / 4095.0f) * 3.3f;

  // Change LOW to HIGH if the LDR behaves opposite
  const int LDR_LIGHT_DETECTED = LOW;

  bool detectedLight = (ldrRaw == LDR_LIGHT_DETECTED);

  lightOn = detectedLight;
}


// ==================================================
// ACS712 5A CURRENT MONITORING - GPIO 10
// ==================================================

void readACS712(unsigned long now) {
  if (now - lastAcsRead < ACS_INTERVAL) {
    return;
  }
  lastAcsRead = now;

  unsigned long totalMv = 0;
  for (int i = 0; i < ACS_SAMPLES; i++) {
    totalMv += analogReadMilliVolts(ACS_PIN);
    delayMicroseconds(500);
  }

  // Voltage measured at GPIO 10. This assumes direct connection,
  // with no voltage divider between ACS712 OUT and GPIO 10.
  acsVoltage = (totalMv / (float)ACS_SAMPLES) / 1000.0f;

  currentAmps = fabsf(acsVoltage - acsZeroVoltage) / ACS_SENSITIVITY;
  currentMilliAmps = currentAmps * 1000.0f;

  Serial.print("ACS712 Voltage: ");
  Serial.print(acsVoltage, 3);
  Serial.print(" V | Current: ");
  Serial.print(currentAmps, 3);
  Serial.print(" A | ");
  Serial.print(currentMilliAmps, 0);
  Serial.println(" mA");
}

// ==================================================
// FAN: RELAY 1
// ==================================================


void controlFan() {
  bool classroomOccupied =
      (studentCount >= OCCUPIED_THRESHOLD);

  bool shouldRunFan =
      classroomOccupied &&
      dhtOK &&
      (temperature > TEMP_THRESHOLD);

  if (shouldRunFan != fanOn) {
    fanOn = shouldRunFan;

    digitalWrite(
      FAN_RELAY,
      fanOn ? RELAY_ON : RELAY_OFF
    );

    Serial.println(fanOn ? "FAN ON" : "FAN OFF");
  }
}


// ==================================================
// LED STRIP: RELAY 2
// ==================================================


void controlLight() {
  bool classroomOccupied =
      (studentCount >= OCCUPIED_THRESHOLD);

  bool shouldTurnOnLED =
      classroomOccupied && !lightOn;

  if (shouldTurnOnLED != (digitalRead(LED_RELAY) == RELAY_ON)) {
    digitalWrite(
      LED_RELAY,
      shouldTurnOnLED ? RELAY_ON : RELAY_OFF
    );
  }
}


// ==================================================
// MQ-2 GAS MONITORING
// ==================================================


void monitorGas(unsigned long now) {
  gasUnsafe = (mq2Raw > GAS_THRESHOLD);

  // Start a continuous 5-second gas alarm
  if (gasUnsafe && !previousGasUnsafe && !buzzerActive) {

    activeBuzzerDuration = GAS_BUZZER_DURATION;

    buzzerActive = true;
    buzzerOutputOn = true;

    digitalWrite(BUZZER_PIN, BUZZER_ON);
    buzzerTimer = now;
  }

  previousGasUnsafe = gasUnsafe;
}


// ==================================================
// NON-BLOCKING BUZZER: DURATION SELECTED BY ALERT
// ==================================================

void updateBuzzer(unsigned long now) {
  if (!buzzerActive) {
    return;
  }

  // Keep buzzer ON continuously for 5 seconds
  if (now - buzzerTimer >= activeBuzzerDuration) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF);

    buzzerOutputOn = false;
    buzzerActive = false;
  }
}


// ==================================================
// Check classroom status
// ==================================================


void checkClassroomStatus(unsigned long now) {
  bool classroomOccupied =
      (studentCount >= OCCUPIED_THRESHOLD);

  // Initialize without triggering an alarm at startup
  if (!classStateInitialized) {
    previousClassOccupied = classroomOccupied;
    classStateInitialized = true;
    return;
  }

  // Beep for 3 seconds when class status changes
  if (classroomOccupied != previousClassOccupied) {
    activeBuzzerDuration = CLASS_BUZZER_DURATION;
    buzzerActive = true;
    buzzerOutputOn = true;

    digitalWrite(BUZZER_PIN, BUZZER_ON);
    buzzerTimer = now;

    previousClassOccupied = classroomOccupied;
  }
}


// ==================================================
// TWO-IR STUDENT COUNTER
// IR1 -> IR2: IN (+1)
// IR2 -> IR1: OUT (-1)
// ==================================================

void updateStudentCounter(unsigned long now) {
  bool ir1 = (digitalRead(IR1_PIN) == IR_DETECTED);
  bool ir2 = (digitalRead(IR2_PIN) == IR_DETECTED);

  // Sequence timeout
  if (irSequence != IR_IDLE &&
      now - irSequenceStart > IR_SEQUENCE_TIMEOUT) {
    irSequence = IR_IDLE;
  }

  switch (irSequence) {
    case IR_IDLE:
      if (ir1 && !ir2 && !previousIr1) {
        irSequence = IR1_FIRST;
        irSequenceStart = now;
      } else if (ir2 && !ir1 && !previousIr2) {
        irSequence = IR2_FIRST;
        irSequenceStart = now;
      }
      break;

    case IR1_FIRST:
      if (ir2 && !ir1) {
        studentCount++;
        Serial.printf("STUDENT ENTERED: %d\n", studentCount);
        irSequence = IR_IDLE;
      }
      break;

    case IR2_FIRST:
      if (ir1 && !ir2) {
        if (studentCount > 0) {
          studentCount--;
        }

        Serial.printf("STUDENT EXITED: %d\n", studentCount);
        irSequence = IR_IDLE;
      }
      break;
  }

  // Require both sensors to clear before accepting another pass.
  if (!ir1 && !ir2) {
    if (lastIrClear == 0) {
      lastIrClear = now;
    }

    if (now - lastIrClear >= IR_CLEAR_TIMEOUT) {
      previousIr1 = false;
      previousIr2 = false;
    }
  } else {
    lastIrClear = 0;
    previousIr1 = ir1;
    previousIr2 = ir2;
  }
}

// ==================================================
// OLED: COMPACT CLASSROOM DASHBOARD
// ==================================================

void updateOLED(unsigned long now) {
  if (now - lastOledUpdate < OLED_INTERVAL) {
    return;
  }

  lastOledUpdate = now;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ICAM CLASSROOM");

  display.setCursor(0, 8);
  display.print("T:");
  if (dhtOK) {
    display.print(temperature, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.println("%");
  } else {
    display.println("DHT ERROR");
  }

  display.setCursor(0, 16);
  display.print("GAS:");
  display.print(gasUnsafe ? "UNSAFE" : "SAFE");
  display.print(" ");
  display.println(mq2Raw);

  display.setCursor(0, 24);
  display.print("STUDENTS:");
  display.print(studentCount);
  display.print(" ");
  display.println(studentCount >= OCCUPIED_THRESHOLD ? "OCCUPIED" : "EMPTY");

  display.setCursor(0, 32);
  display.print("FAN:");
  display.print(fanOn ? "ON" : "OFF");
  display.print(" LED:");
  display.println(digitalRead(LED_RELAY) == RELAY_ON ? "ON" : "OFF");

  display.setCursor(0, 40);
  display.print("CURRENT:");
  display.print(currentMilliAmps, 0);
  display.println(" mA");

  display.display();
}
