// Solar panel anti-theft system
// Written by Tobias

#include <EEPROM.h>

#define EEPROM_CALIBRATION 0
#define EEPROM_ALLOWED_DEVIATION sizeof(unsigned short)
#define EEPROM_DEBOUNCE_TIME_MS 2 * sizeof(unsigned short)
#define EEPROM_STAT_MAX 2 * sizeof(unsigned short) + sizeof(unsigned long)
#define EEPROM_STAT_MIN 3 * sizeof(unsigned short) + sizeof(unsigned long)
#define EEPROM_STAT_ANOMALY_COUNT 4 * sizeof(unsigned short) + sizeof(unsigned long)
#define EEPROM_STAT_ALARM_COUNT 4 * sizeof(unsigned short) + 2 * sizeof(unsigned long)
#define EEPROM_ALARM_ON 4 * sizeof(unsigned short) + 3 * sizeof(unsigned long)
#define EEPROM_ALARM_PULSE_DURATION_MS 4 * sizeof(unsigned short) + 3 * sizeof(unsigned long) + 1
#define EEPROM_ALARM_SILENCE_DURATION_MS 5 * sizeof(unsigned short) + 3 * sizeof(unsigned long) + 1
#define EEPROM_ALARM_TOTAL_PULSES 6 * sizeof(unsigned short) + 3 * sizeof(unsigned long) + 1

// The mean AD reading when the security wire is connected
unsigned short calibration = 0;

// The deviation allowed before an anomaly is detected
unsigned short allowedDeviation = 0;

// The duration for which the anomaly must be present before the alarm goes off
unsigned long debounceTime = 0;

// Largest AD reading taken
unsigned short statMax = 0;

// Smallest AD reading taken
unsigned short statMin = 0;

// Number of times an anomaly was detected
unsigned long statAnomalyCount = 0;

// Number of times an alarm was set off
unsigned long statAlarmCount = 0;

// Switch that primes the alarm
byte alarmOn;

// Duration of the horn "on" period
unsigned short alarmPulseDuration;

// Duration of the horn "off" period
unsigned short alarmSilenceDuration;

// Number of times the horn will have to make noise, use 0 for infinite
unsigned short alarmTotalPulses;

// The previous measurement taken
unsigned short prevMeasurement = 65535;

// The most recent measurement taken
unsigned short measurement = 0;

// Whether an anomaly is currently present
bool anomalyTriggered = false;

// The millis time when the anomaly was detected
unsigned long anomalyTime = 0;

// Whether the alarm is going off/has gone off since the anomaly was detected
bool alarmTriggered = false;

void setup() {
  pinMode(D5, OUTPUT);
  pinMode(A0, INPUT);
  Serial.begin(115200);
  EEPROM.begin(27);
  readEEPROM();
  configMode();
}

void loop() {
  // Backup previous measurement
  prevMeasurement = measurement;

  // Take a new measurement
  measurement = takeMeasurement();

  if (prevMeasurement != measurement) {
    Serial.println("Current measurement: " + String(measurement) + ".");
  }

  // Store new max reading if necessary
  if (measurement > statMax) {
    EEPROM.put(EEPROM_STAT_MAX, measurement);
    EEPROM.commit();
  }
  
  // Store new min reading if necessary
  if (measurement < statMin) {
    EEPROM.put(EEPROM_STAT_MIN, measurement);
    EEPROM.commit();
  }

  // If an anomaly was detected before but is now gone, reset the triggered flags
  if (anomalyTriggered && !isAnomaly()) {
    Serial.println("Resetting anomaly and alarm flags.");
    anomalyTriggered = false;
    alarmTriggered = false;
  }

  // New anomaly found?
  if (!anomalyTriggered && isAnomaly()) {
    Serial.println("Anomaly detected. Starting countdown.");
    
    // Up the anomaly counter
    statAnomalyCount ++;
    EEPROM.put(EEPROM_STAT_ANOMALY_COUNT, statAnomalyCount);
    EEPROM.commit();

    // Set the anomaly flag
    anomalyTriggered = true;

    // Record the time
    anomalyTime = millis();
  }

  // Anomaly flag set, and the grace period has passed? Set off the alarm
  if (anomalyTriggered && !alarmTriggered && millis() - anomalyTime >= debounceTime) {
    Serial.println("Setting off alarm.");
    
    statAlarmCount ++;
    EEPROM.put(EEPROM_STAT_ALARM_COUNT, statAlarmCount);
    EEPROM.commit();

    alarmTriggered = true;
    alarm();
  }
  
  delay(100);
}

// Read the value provided by the AD converter 4 times and take the mean
unsigned short takeMeasurement() {
  unsigned int a = analogRead(A0);
  delay(5);
  unsigned int b = analogRead(A0);
  delay(5);
  unsigned int c = analogRead(A0);
  delay(5);
  unsigned int d = analogRead(A0);
  return (a + b + c + d) / 4;
}

// Sound the alarm
void alarm() {
  if (!alarmOn) {
    return;
  }
  
  unsigned short pulses = 0;
  unsigned short target = alarmTotalPulses;
  if (alarmTotalPulses == 0) {
    target = 1;
  }
  
  while (pulses < target) {
    digitalWrite(D5, HIGH);
    delay(alarmPulseDuration);
    digitalWrite(D5, LOW);
    delay(alarmSilenceDuration);
    
    if (alarmTotalPulses != 0) {
      pulses ++;
    }
  }
}

// Helper that checks whether a measurement is out of bounds
bool isAnomaly() {
  // Wire snipped
  if (measurement == 1024) {
    return true;
  }
  
  if (measurement > calibration + allowedDeviation) {
    return true;
  }

  if (allowedDeviation <= calibration && measurement < calibration - allowedDeviation) {
    return true;
  }

  return false;
}

// Read stuff from the EEPROM into global variables
void readEEPROM() {
  EEPROM.get(EEPROM_CALIBRATION, calibration);
  EEPROM.get(EEPROM_ALLOWED_DEVIATION, allowedDeviation);
  EEPROM.get(EEPROM_DEBOUNCE_TIME_MS, debounceTime);
  EEPROM.get(EEPROM_STAT_MAX, statMax);
  EEPROM.get(EEPROM_STAT_MIN, statMin);
  EEPROM.get(EEPROM_STAT_ANOMALY_COUNT, statAnomalyCount);
  EEPROM.get(EEPROM_STAT_ALARM_COUNT, statAlarmCount);
  EEPROM.get(EEPROM_ALARM_ON, alarmOn);
  EEPROM.get(EEPROM_ALARM_PULSE_DURATION_MS, alarmPulseDuration);
  EEPROM.get(EEPROM_ALARM_SILENCE_DURATION_MS, alarmSilenceDuration);
  EEPROM.get(EEPROM_ALARM_TOTAL_PULSES, alarmTotalPulses);
}

// Prompt for config mode
void configMode() {
  unsigned long startTime = millis();

  Serial.println();
  Serial.println("Please press a key to enter configuration mode within 5 seconds.");
  Serial.flush();
  
  while (millis() - startTime < 5000 && Serial.available() == 0);

  if (Serial.available() != 0) {
    while (Serial.available() != 0) {
      Serial.read();
    }
    configMenu();
    waitForInput();
  }
}

void configMenu() {
  readEEPROM();
  
  Serial.println();
  Serial.println("Select a parameter to change.");
  Serial.println();
  Serial.println("0. CALIBRATION = " + String(calibration));
  Serial.println("1. ALLOWED_DEVIATION = " + String(allowedDeviation));
  Serial.println("2. DEBOUNCE_TIME_MS = " + String(debounceTime));
  Serial.println("3. STAT_MAX = " + String(statMax));
  Serial.println("4. STAT_MIN = " + String(statMin));
  Serial.println("5. STAT_ANOMALY_COUNT = " + String(statAnomalyCount));
  Serial.println("6. STAT_ALARM_COUNT = " + String(statAlarmCount));
  Serial.println("7. ALARM_ON = " + String(alarmOn));
  Serial.println("8. ALARM_PULSE_DURATION_MS = " + String(alarmPulseDuration));
  Serial.println("9. ALARM_SILENCE_DURATION_MS = " + String(alarmSilenceDuration));
  Serial.println("A. ALARM_TOTAL_PULSES = " + String(alarmTotalPulses));
  Serial.println();
  Serial.println("B. Auto calibrate");
  Serial.println("C. Validate");
  Serial.println("D. Factory reset");
  Serial.println("E. Exit");
  Serial.println();
  Serial.flush();
}

void waitForInput() {
  while (Serial.available() == 0);

  if (Serial.available() != 1) {
    Serial.println("Do not enter more than one character.");
    configMenu();
    waitForInput();
    return;
  }

  byte choice = Serial.read() - 48;

  if (choice > 9 && choice != 17 && choice != 18 && choice != 19 && choice != 20 && choice != 21) {
    Serial.println("Invalid answer. Try again.");
    configMenu();
    waitForInput();
    return;
  }

  if (choice == 18) {
    Serial.println("Auto calibrating...");

    unsigned short a = analogRead(A0);
    delay(5);
    unsigned short b = analogRead(A0);
    unsigned short diff = a > b ? a - b : b - a;
    diff *= 2;
    if (diff < 2) {
      diff = 2;
    }
    EEPROM.put(EEPROM_ALLOWED_DEVIATION, diff);
    unsigned int c = a;
    c += b;
    c /= 2;
    a = c;
    EEPROM.put(EEPROM_CALIBRATION, a);
    EEPROM.commit();

    configMenu();
    waitForInput();
    return;
  }

  if (choice == 19) {
    bool remark = false;
    
    Serial.println("Validating...");
    Serial.println();
    if (calibration == 0) {
      remark = true;
      Serial.println("CALIBRATION not yet set. Use STAT_MAX and STAT_MIN values to determine.");
    }

    if (allowedDeviation < 2) {
      remark = true;
      Serial.println("ALLOWED_DEVIATION is quite low. A value of at least 2 is recommended for stability.");
    }

    if (allowedDeviation > 50) {
      remark = true;
      Serial.println("ALLOWED_DEVIATION is quite high. Is your security wire soundly connected?.");
    }

    if (debounceTime < 1000) {
      remark = true;
      Serial.println("DEBOUNCE_TIME_MS is quite low. A value of at least 1000 ms is recommended for stability.");
    }

    if (alarmPulseDuration < 500) {
      remark = true;
      Serial.println("ALARM_PULSE_DURATION_MS is quite low. A value of at least 500 ms is recommended.");
    }

    if (alarmSilenceDuration < 500) {
      remark = true;
      Serial.println("ALARM_SILENCE_DURATION_MS is quite low. A value of at least 500 ms is recommended.");
    }

    if (alarmTotalPulses == 0) {
      remark = true;
      Serial.println("ALARM_TOTAL_PULSES is zero. This means that the alarm will sound indefinitely.");
    }

    if (!remark) {
      Serial.println("Validation complete. No remarks found.");
    }
    
    configMenu();
    waitForInput();
    return;
  }

  unsigned long valueLong;
  unsigned short valueShort;
  byte valueByte;

  if (choice == 20) {
    Serial.println("Resetting all parameters...");
    valueShort = 512;
    EEPROM.put(EEPROM_CALIBRATION, valueShort);
    valueShort = 5;
    EEPROM.put(EEPROM_ALLOWED_DEVIATION, valueShort);
    valueLong = 10000;
    EEPROM.put(EEPROM_DEBOUNCE_TIME_MS, valueLong);
    valueShort = 0;
    EEPROM.put(EEPROM_STAT_MAX, valueShort);
    valueShort = 1024;
    EEPROM.put(EEPROM_STAT_MIN, valueShort);
    valueLong = 0;
    EEPROM.put(EEPROM_STAT_ANOMALY_COUNT, valueLong);
    EEPROM.put(EEPROM_STAT_ALARM_COUNT, valueLong);
    valueByte = 0;
    EEPROM.put(EEPROM_ALARM_ON, valueByte);
    valueShort = 1250;
    EEPROM.put(EEPROM_ALARM_PULSE_DURATION_MS, valueShort);
    EEPROM.put(EEPROM_ALARM_SILENCE_DURATION_MS, valueShort);
    valueShort = 100;
    EEPROM.put(EEPROM_ALARM_TOTAL_PULSES, valueShort);
    EEPROM.commit();
    
    configMenu();
    waitForInput();
    return;
  }

  if (choice == 21) {
    Serial.println("Exiting.");
    return;
  }

  Serial.println("Please enter a new value.");
  Serial.println();
  Serial.flush();

  while (Serial.available() == 0);

  valueLong = Serial.parseInt();

  if (choice == 0 || choice == 1 || choice == 3 || choice == 4) {
    if (valueLong > 1024) {
      Serial.println("This value is unfit for this data type (AD converter value).");
      configMenu();
      waitForInput();
      return;
    }

    valueShort = (unsigned short) valueLong;

    EEPROM.put(getAddress(choice), valueShort);
  } else if (choice == 8 || choice == 9 || choice == 17) {
    if (valueLong > 65535) {
      Serial.println("This value is unfit for this data type (unsigned short).");
      configMenu();
      waitForInput();
      return;
    }

    valueShort = (unsigned short) valueLong;

    EEPROM.put(getAddress(choice), valueShort);
  } else if (choice == 7) {
    if (valueLong != 0 && valueLong != 1) {
      Serial.println("This value is unfit for this data type (bool).");
      configMenu();
      waitForInput();
      return;
    }

    valueByte = (byte) valueLong;
    
    EEPROM.put(getAddress(choice), valueByte);
  } else {
    EEPROM.put(getAddress(choice), valueLong);
  }

  EEPROM.commit();

  Serial.println("New value stored.");
  Serial.flush();
  
  configMenu();
  waitForInput();
}

// helper that returns EEPROM addresses
unsigned int getAddress(byte choice) {
  switch (choice) {
    case 0: return EEPROM_CALIBRATION;
    case 1: return EEPROM_ALLOWED_DEVIATION;
    case 2: return EEPROM_DEBOUNCE_TIME_MS;
    case 3: return EEPROM_STAT_MAX;
    case 4: return EEPROM_STAT_MIN;
    case 5: return EEPROM_STAT_ANOMALY_COUNT;
    case 6: return EEPROM_STAT_ALARM_COUNT;
    case 7: return EEPROM_ALARM_ON;
    case 8: return EEPROM_ALARM_PULSE_DURATION_MS;
    case 9: return EEPROM_ALARM_SILENCE_DURATION_MS;
    case 17: return EEPROM_ALARM_TOTAL_PULSES;
  }
}
