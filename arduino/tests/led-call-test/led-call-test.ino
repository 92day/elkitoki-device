#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int RED_LED_PIN = 7;
const int GREEN_LED_PIN = 2;

char serialLine[128];
size_t serialLineLen = 0;

bool redLedState = false;
bool greenLedState = true;

void setIndicator(bool redOn, bool greenOn) {
  redLedState = redOn;
  greenLedState = greenOn;

  digitalWrite(RED_LED_PIN, redLedState ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, greenLedState ? HIGH : LOW);
}

void drawStatus(const char* line1, const char* line2) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 14, line1);
    u8g2.drawStr(0, 34, line2);
  } while (u8g2.nextPage());
}

void emitEvent(const char* eventType, const char* worker) {
  Serial.print("{\"kind\":\"event\",\"device\":\"uno-main\",\"eventType\":\"");
  Serial.print(eventType);
  Serial.print("\"");
  if (worker != NULL) {
    Serial.print(",\"worker\":\"");
    Serial.print(worker);
    Serial.print("\"");
  }
  Serial.println("}");
}

bool containsToken(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != NULL;
}

void applyWorkerCall(const char* worker) {
  setIndicator(true, false);
  if (strcmp(worker, "B") == 0) {
    drawStatus("CALL COMMAND", "WORKER B");
  } else {
    drawStatus("CALL COMMAND", "WORKER A");
  }
  emitEvent("command_applied", worker);
}

void clearOutputs() {
  setIndicator(false, true);
  drawStatus("READY", "WAITING");
  emitEvent("clear_outputs", NULL);
}

void handleCommand(const char* line) {
  if (containsToken(line, "\"cmd\":\"call_worker\"")) {
    if (containsToken(line, "\"worker\":\"B\"")) {
      applyWorkerCall("B");
    } else {
      applyWorkerCall("A");
    }
    return;
  }

  if (containsToken(line, "\"cmd\":\"clear_outputs\"")) {
    clearOutputs();
    return;
  }

  drawStatus("UNKNOWN CMD", "CHECK JSON");
  emitEvent("unknown_command", NULL);
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
      drawStatus("SERIAL ERROR", "LINE TOO LONG");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  clearOutputs();
}

void loop() {
  readSerialCommands();
  delay(20);
}
