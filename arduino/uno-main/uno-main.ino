#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int RED_LED_PIN = 7;
const int GREEN_LED_PIN = 2;
const int HEART_PIN = A0;
const int SOUND_A_PIN = A1;
const int SOUND_B_PIN = A2;
const int SOUND_C_PIN = A3;
const int TILT_PIN = 3;
const int MANUAL_BUTTON_PIN = 4;
const int MOTOR_PIN = 5;

const bool ENABLE_HEART_SENSOR = true;
const bool ENABLE_SOUND_SENSORS = false;
const bool ENABLE_TILT_SENSOR = false;
const bool ENABLE_MANUAL_BUTTON = false;
const bool ENABLE_MOTOR = false;

const int HEART_ALERT_THRESHOLD = 700;
const int SOUND_ALERT_THRESHOLD = 700;

const unsigned long STATUS_INTERVAL_MS = 1000;
const unsigned long CALL_OUTPUT_MS = 5000;

char serialLine[128];
size_t serialLineLen = 0;

bool redLedState = false;
bool greenLedState = true;
bool motorState = false;
bool heartAlertActive = false;
bool tiltAlertActive = false;
bool manualPressedState = false;
bool callOutputActive = false;

unsigned long lastStatusAt = 0;
unsigned long callOutputUntil = 0;

void setOutputs(bool redOn, bool greenOn, bool motorOn) {
  redLedState = redOn;
  greenLedState = greenOn;
  motorState = motorOn && ENABLE_MOTOR;

  digitalWrite(RED_LED_PIN, redLedState ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, greenLedState ? HIGH : LOW);

  if (ENABLE_MOTOR) {
    digitalWrite(MOTOR_PIN, motorState ? HIGH : LOW);
  }
}

void showDisplay(const char* topText, int heartRaw, bool fingerDetected) {
  char line[24];

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 12, topText);

    if (fingerDetected) {
      snprintf(line, sizeof(line), "RAW: %d", heartRaw);
      u8g2.drawStr(0, 32, line);
      u8g2.drawStr(0, 52, "FINGER OK");
    } else {
      u8g2.drawStr(0, 32, "RAW: --");
      u8g2.drawStr(0, 52, "NO FINGER");
    }
  } while (u8g2.nextPage());
}

void emitStatus(int heartRaw, bool fingerDetected, int soundA, int soundB, int soundC) {
  Serial.print("{\"kind\":\"status\",\"device\":\"uno-main\"");
  Serial.print(",\"heartRaw\":");
  Serial.print(fingerDetected ? heartRaw : 0);
  Serial.print(",\"fingerDetected\":");
  Serial.print(fingerDetected ? "true" : "false");
  Serial.print(",\"redLed\":");
  Serial.print(redLedState ? "true" : "false");
  Serial.print(",\"greenLed\":");
  Serial.print(greenLedState ? "true" : "false");
  Serial.print(",\"manualPressed\":");
  Serial.print(manualPressedState ? "true" : "false");
  Serial.print(",\"soundA\":");
  Serial.print(soundA);
  Serial.print(",\"soundB\":");
  Serial.print(soundB);
  Serial.print(",\"soundC\":");
  Serial.print(soundC);
  Serial.print(",\"tiltAlert\":");
  Serial.print(tiltAlertActive ? "true" : "false");
  Serial.print(",\"motorActive\":");
  Serial.print(motorState ? "true" : "false");
  Serial.println("}");
}

void emitHeartEvent(int heartRaw, bool fingerDetected, bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"heart_abnormal\",\"worker\":\"A\",\"heartRaw\":");
  Serial.print(fingerDetected ? heartRaw : 0);
  Serial.print(",\"threshold\":");
  Serial.print(HEART_ALERT_THRESHOLD);
  Serial.print(",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitManualButtonEvent(bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"worker_call_button\",\"source\":\"manual_button\",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitNoiseEvent(const char* zone, int value, bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"noise_abnormal\",\"zone\":\"");
  Serial.print(zone);
  Serial.print("\",\"value\":");
  Serial.print(value);
  Serial.print(",\"threshold\":");
  Serial.print(SOUND_ALERT_THRESHOLD);
  Serial.print(",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitTiltEvent(bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"fall_detected\",\"worker\":\"A\",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitCommandApplied(const char* commandName) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"command_applied\",\"command\":\"");
  Serial.print(commandName);
  Serial.println("\"}");
}

void emitError(const char* message) {
  Serial.print("{\"kind\":\"error\",\"message\":\"");
  Serial.print(message);
  Serial.println("\"}");
}

bool containsToken(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != NULL;
}

void applyWorkerCall(const char* worker) {
  callOutputActive = true;
  callOutputUntil = millis() + CALL_OUTPUT_MS;
  setOutputs(true, false, true);
  showDisplay(strcmp(worker, "B") == 0 ? "CALL B" : "CALL A", analogRead(HEART_PIN), analogRead(HEART_PIN) > 500);
}

void clearCallOutputs() {
  callOutputActive = false;
  callOutputUntil = 0;
  setOutputs(false, true, false);
}

void handleCommand(const char* line) {
  if (containsToken(line, "\"cmd\":\"call_worker\"")) {
    if (containsToken(line, "\"worker\":\"B\"")) {
      applyWorkerCall("B");
    } else {
      applyWorkerCall("A");
    }
    emitCommandApplied("call_worker");
    return;
  }

  if (containsToken(line, "\"cmd\":\"clear_outputs\"")) {
    clearCallOutputs();
    emitCommandApplied("clear_outputs");
    return;
  }

  if (containsToken(line, "\"cmd\":\"set_indicator\"")) {
    bool redRequested = containsToken(line, "\"color\":\"red\"");
    bool greenRequested = containsToken(line, "\"color\":\"green\"");
    bool turnOn = containsToken(line, "\"state\":\"on\"");

    if (redRequested) {
      setOutputs(turnOn, greenLedState && !turnOn, motorState);
    } else if (greenRequested) {
      setOutputs(redLedState && !turnOn, turnOn, motorState);
    } else {
      emitError("unknown indicator color");
      return;
    }

    emitCommandApplied("set_indicator");
    return;
  }

  emitError("unknown command");
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char incoming = (char)Serial.read();

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      if (serialLineLen > 0) {
        serialLine[serialLineLen] = '\0';
        handleCommand(serialLine);
        serialLineLen = 0;
      }
      continue;
    }

    if (serialLineLen < sizeof(serialLine) - 1) {
      serialLine[serialLineLen++] = incoming;
    } else {
      serialLineLen = 0;
      emitError("command too long");
    }
  }
}

void setupFeaturePins() {
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  if (ENABLE_MOTOR) {
    pinMode(MOTOR_PIN, OUTPUT);
  }

  if (ENABLE_TILT_SENSOR) {
    pinMode(TILT_PIN, INPUT_PULLUP);
  }

  if (ENABLE_MANUAL_BUTTON) {
    pinMode(MANUAL_BUTTON_PIN, INPUT_PULLUP);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  setupFeaturePins();
  setOutputs(false, true, false);
  showDisplay("READY", 0, false);
}

void loop() {
  readSerialCommands();

  int heartRaw = ENABLE_HEART_SENSOR ? analogRead(HEART_PIN) : 0;
  bool fingerDetected = heartRaw > 500;

  if (!callOutputActive) {
    showDisplay(greenLedState ? "GREEN" : "RED", heartRaw, fingerDetected);
  }

  bool nextHeartAlert = ENABLE_HEART_SENSOR && fingerDetected && heartRaw >= HEART_ALERT_THRESHOLD;
  if (nextHeartAlert != heartAlertActive) {
    heartAlertActive = nextHeartAlert;
    emitHeartEvent(heartRaw, fingerDetected, heartAlertActive);
  }

  int soundA = 0;
  int soundB = 0;
  int soundC = 0;

  if (ENABLE_SOUND_SENSORS) {
    soundA = analogRead(SOUND_A_PIN);
    soundB = analogRead(SOUND_B_PIN);
    soundC = analogRead(SOUND_C_PIN);

    if (soundA >= SOUND_ALERT_THRESHOLD) emitNoiseEvent("A", soundA, true);
    if (soundB >= SOUND_ALERT_THRESHOLD) emitNoiseEvent("B", soundB, true);
    if (soundC >= SOUND_ALERT_THRESHOLD) emitNoiseEvent("C", soundC, true);
  }

  if (ENABLE_TILT_SENSOR) {
    bool nextTiltAlert = digitalRead(TILT_PIN) == LOW;
    if (nextTiltAlert != tiltAlertActive) {
      tiltAlertActive = nextTiltAlert;
      emitTiltEvent(tiltAlertActive);
    }
  }

  if (ENABLE_MANUAL_BUTTON) {
    bool nextManualPressed = digitalRead(MANUAL_BUTTON_PIN) == LOW;
    if (nextManualPressed != manualPressedState) {
      manualPressedState = nextManualPressed;
      emitManualButtonEvent(manualPressedState);
    }
  }

  if (callOutputActive && millis() >= callOutputUntil) {
    clearCallOutputs();
  }

  if (millis() - lastStatusAt >= STATUS_INTERVAL_MS) {
    lastStatusAt = millis();
    emitStatus(heartRaw, fingerDetected, soundA, soundB, soundC);
  }

  delay(30);
}


