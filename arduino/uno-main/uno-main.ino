#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int HEART_A_PIN = A0;
const int HEART_B_PIN = A1;
const int BUTTON_A_PIN = 2;
const int BUTTON_B_PIN = 3;
const int MOTOR_A_PIN = 5;
const int MOTOR_B_PIN = 6;
const int LED_A_PIN = 7;
const int LED_B_PIN = 8;

const int HEART_ALERT_THRESHOLD = 700;
const unsigned long STATUS_INTERVAL_MS = 1000;
const unsigned long CALL_OUTPUT_MS = 1000;
const unsigned long DISPLAY_ROTATE_MS = 1800;

char serialLine[128];
size_t serialLineLen = 0;

bool ledStateA = false;
bool ledStateB = false;
bool motorStateA = false;
bool motorStateB = false;
bool heartAlertActiveA = false;
bool heartAlertActiveB = false;
bool buttonPressedA = false;
bool buttonPressedB = false;
bool callActiveA = false;
bool callActiveB = false;
bool displayHeartPage = true;

unsigned long lastStatusAt = 0;
unsigned long displaySwapAt = 0;
unsigned long callOutputUntilA = 0;
unsigned long callOutputUntilB = 0;

void applyOutputs() {
  digitalWrite(LED_A_PIN, ledStateA ? HIGH : LOW);
  digitalWrite(LED_B_PIN, ledStateB ? HIGH : LOW);
  digitalWrite(MOTOR_A_PIN, motorStateA ? HIGH : LOW);
  digitalWrite(MOTOR_B_PIN, motorStateB ? HIGH : LOW);
}

void showHeartDisplay(int heartRawA, bool fingerDetectedA, int heartRawB, bool fingerDetectedB) {
  char lineA[28];
  char lineB[28];

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 12, "HEART STATUS");
    snprintf(lineA, sizeof(lineA), "A %4d %s", fingerDetectedA ? heartRawA : 0, fingerDetectedA ? "OK" : "--");
    snprintf(lineB, sizeof(lineB), "B %4d %s", fingerDetectedB ? heartRawB : 0, fingerDetectedB ? "OK" : "--");
    u8g2.drawStr(0, 32, lineA);
    u8g2.drawStr(0, 52, lineB);
  } while (u8g2.nextPage());
}

void showCallDisplay() {
  char lineA[24];
  char lineB[24];

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 12, "CALL / BUTTON");
    snprintf(lineA, sizeof(lineA), "A BTN:%s CALL:%s", buttonPressedA ? "ON" : "OFF", callActiveA ? "ON" : "OFF");
    snprintf(lineB, sizeof(lineB), "B BTN:%s CALL:%s", buttonPressedB ? "ON" : "OFF", callActiveB ? "ON" : "OFF");
    u8g2.drawStr(0, 32, lineA);
    u8g2.drawStr(0, 52, lineB);
  } while (u8g2.nextPage());
}

void showCallOverlay(const char* worker) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 14, "WORKER CALL");
    u8g2.setFont(u8g2_font_logisoso24_tr);
    u8g2.drawStr(16, 52, worker);
  } while (u8g2.nextPage());
}

void refreshDisplay(int heartRawA, bool fingerDetectedA, int heartRawB, bool fingerDetectedB) {
  if (callActiveA) {
    showCallOverlay("A");
    return;
  }
  if (callActiveB) {
    showCallOverlay("B");
    return;
  }
  if (displayHeartPage) {
    showHeartDisplay(heartRawA, fingerDetectedA, heartRawB, fingerDetectedB);
  } else {
    showCallDisplay();
  }
}

void emitStatus(int heartRawA, bool fingerDetectedA, int heartRawB, bool fingerDetectedB) {
  Serial.print("{\"kind\":\"status\",\"device\":\"uno-main\"");
  Serial.print(",\"heartRawA\":");
  Serial.print(fingerDetectedA ? heartRawA : 0);
  Serial.print(",\"heartRawB\":");
  Serial.print(fingerDetectedB ? heartRawB : 0);
  Serial.print(",\"fingerA\":");
  Serial.print(fingerDetectedA ? "true" : "false");
  Serial.print(",\"fingerB\":");
  Serial.print(fingerDetectedB ? "true" : "false");
  Serial.print(",\"buttonPressedA\":");
  Serial.print(buttonPressedA ? "true" : "false");
  Serial.print(",\"buttonPressedB\":");
  Serial.print(buttonPressedB ? "true" : "false");
  Serial.print(",\"callActiveA\":");
  Serial.print(callActiveA ? "true" : "false");
  Serial.print(",\"callActiveB\":");
  Serial.print(callActiveB ? "true" : "false");
  Serial.println("}");
}

void emitHeartEvent(const char* worker, int heartRaw, bool fingerDetected, bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"heart_abnormal\",\"worker\":\"");
  Serial.print(worker);
  Serial.print("\",\"value\":");
  Serial.print(fingerDetected ? heartRaw : 0);
  Serial.print(",\"heartRaw\":");
  Serial.print(fingerDetected ? heartRaw : 0);
  Serial.print(",\"threshold\":");
  Serial.print(HEART_ALERT_THRESHOLD);
  Serial.print(",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitManualButtonEvent(const char* worker, bool active) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"worker_call_button\",\"worker\":\"");
  Serial.print(worker);
  Serial.print("\",\"source\":\"manual_button_");
  Serial.print(worker);
  Serial.print("\",\"active\":");
  Serial.print(active ? "true" : "false");
  Serial.println("}");
}

void emitCommandApplied(const char* commandName, const char* worker) {
  Serial.print("{\"kind\":\"event\",\"eventType\":\"command_applied\",\"command\":\"");
  Serial.print(commandName);
  Serial.print("\"");
  if (worker != NULL) {
    Serial.print(",\"worker\":\"");
    Serial.print(worker);
    Serial.print("\"");
  }
  Serial.println("}");
}

void emitError(const char* message) {
  Serial.print("{\"kind\":\"error\",\"message\":\"");
  Serial.print(message);
  Serial.println("\"}");
}

bool containsToken(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != NULL;
}

void setWorkerCallState(const char* worker, bool active) {
  if (strcmp(worker, "B") == 0) {
    callActiveB = active;
    ledStateB = active;
    motorStateB = active;
    callOutputUntilB = active ? millis() + CALL_OUTPUT_MS : 0;
  } else {
    callActiveA = active;
    ledStateA = active;
    motorStateA = active;
    callOutputUntilA = active ? millis() + CALL_OUTPUT_MS : 0;
  }
  applyOutputs();
}

void applyWorkerCall(const char* worker) {
  setWorkerCallState("A", false);
  setWorkerCallState("B", false);
  setWorkerCallState(worker, true);
}

void handleCommand(const char* line) {
  if (containsToken(line, "\"cmd\":\"call_worker\"")) {
    if (containsToken(line, "\"worker\":\"B\"")) {
      applyWorkerCall("B");
      emitCommandApplied("call_worker", "B");
    } else {
      applyWorkerCall("A");
      emitCommandApplied("call_worker", "A");
    }
    return;
  }

  if (containsToken(line, "\"cmd\":\"clear_outputs\"")) {
    setWorkerCallState("A", false);
    setWorkerCallState("B", false);
    emitCommandApplied("clear_outputs", NULL);
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
  pinMode(LED_A_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  pinMode(MOTOR_A_PIN, OUTPUT);
  pinMode(MOTOR_B_PIN, OUTPUT);
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  setupFeaturePins();
  applyOutputs();
  refreshDisplay(0, false, 0, false);
}

void loop() {
  readSerialCommands();

  int heartRawA = analogRead(HEART_A_PIN);
  int heartRawB = analogRead(HEART_B_PIN);
  bool fingerDetectedA = heartRawA > 500;
  bool fingerDetectedB = heartRawB > 500;

  if (millis() - displaySwapAt >= DISPLAY_ROTATE_MS) {
    displaySwapAt = millis();
    displayHeartPage = !displayHeartPage;
  }

  refreshDisplay(heartRawA, fingerDetectedA, heartRawB, fingerDetectedB);

  bool nextHeartAlertA = fingerDetectedA && heartRawA >= HEART_ALERT_THRESHOLD;
  if (nextHeartAlertA != heartAlertActiveA) {
    heartAlertActiveA = nextHeartAlertA;
    emitHeartEvent("A", heartRawA, fingerDetectedA, heartAlertActiveA);
  }

  bool nextHeartAlertB = fingerDetectedB && heartRawB >= HEART_ALERT_THRESHOLD;
  if (nextHeartAlertB != heartAlertActiveB) {
    heartAlertActiveB = nextHeartAlertB;
    emitHeartEvent("B", heartRawB, fingerDetectedB, heartAlertActiveB);
  }

  bool nextButtonPressedA = digitalRead(BUTTON_A_PIN) == LOW;
  if (nextButtonPressedA != buttonPressedA) {
    buttonPressedA = nextButtonPressedA;
    if (buttonPressedA) {
      applyWorkerCall("A");
    }
    emitManualButtonEvent("A", buttonPressedA);
  }

  bool nextButtonPressedB = digitalRead(BUTTON_B_PIN) == LOW;
  if (nextButtonPressedB != buttonPressedB) {
    buttonPressedB = nextButtonPressedB;
    if (buttonPressedB) {
      applyWorkerCall("B");
    }
    emitManualButtonEvent("B", buttonPressedB);
  }

  if (callActiveA && millis() >= callOutputUntilA) {
    setWorkerCallState("A", false);
  }

  if (callActiveB && millis() >= callOutputUntilB) {
    setWorkerCallState("B", false);
  }

  if (millis() - lastStatusAt >= STATUS_INTERVAL_MS) {
    lastStatusAt = millis();
    emitStatus(heartRawA, fingerDetectedA, heartRawB, fingerDetectedB);
  }

  delay(30);
}

