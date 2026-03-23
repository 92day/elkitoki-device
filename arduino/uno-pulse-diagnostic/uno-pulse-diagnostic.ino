#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int SENSOR_PIN = A0;
const int MIN_THRESHOLD = 6;
const int THRESHOLD_MARGIN = 3;
const unsigned long DISPLAY_INTERVAL_MS = 100;

int rawValue = 0;
int smoothValue = 0;
long baselineScaled = 0;
int baselineValue = 0;
int signalValue = 0;
int signalMin = 0;
int signalMax = 0;
int signalRange = 0;
int thresholdValue = MIN_THRESHOLD;

bool initialized = false;
unsigned long lastWindowAt = 0;
unsigned long lastDisplayAt = 0;

void updateSignal() {
  rawValue = analogRead(SENSOR_PIN);

  if (!initialized) {
    smoothValue = rawValue;
    baselineScaled = (long)rawValue * 16L;
    baselineValue = rawValue;
    signalValue = 0;
    signalMin = 0;
    signalMax = 0;
    signalRange = 0;
    lastWindowAt = millis();
    initialized = true;
    return;
  }

  smoothValue = (smoothValue * 5 + rawValue) / 6;
  baselineScaled = (baselineScaled * 31L + (long)smoothValue * 16L) / 32L;
  baselineValue = (int)(baselineScaled / 16L);
  signalValue = smoothValue - baselineValue;

  if (signalValue < signalMin) {
    signalMin = signalValue;
  }
  if (signalValue > signalMax) {
    signalMax = signalValue;
  }

  unsigned long now = millis();
  if (now - lastWindowAt >= 800) {
    signalRange = signalMax - signalMin;
    thresholdValue = signalRange / 4 + THRESHOLD_MARGIN;
    if (thresholdValue < MIN_THRESHOLD) {
      thresholdValue = MIN_THRESHOLD;
    }
    signalMin = signalValue;
    signalMax = signalValue;
    lastWindowAt = now;
  }
}

void drawDisplay() {
  char line1[24];
  char line2[24];
  char line3[24];
  char line4[24];

  snprintf(line1, sizeof(line1), "RAW  %4d", rawValue);
  snprintf(line2, sizeof(line2), "BASE %4d", baselineValue);
  snprintf(line3, sizeof(line3), "SIG  %4d", signalValue);
  snprintf(line4, sizeof(line4), "RNG %3d TH %2d", signalRange, thresholdValue);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "A0 SERIAL PLOTTER");
    u8g2.drawStr(0, 26, line1);
    u8g2.drawStr(0, 38, line2);
    u8g2.drawStr(0, 50, line3);
    u8g2.drawStr(0, 62, line4);
  } while (u8g2.nextPage());
}

void emitPlotterLine() {
  int thresholdHigh = baselineValue + thresholdValue;
  int thresholdLow = baselineValue - thresholdValue;

  Serial.print("raw:");
  Serial.print(rawValue);
  Serial.print('\t');

  Serial.print("smooth:");
  Serial.print(smoothValue);
  Serial.print('\t');

  Serial.print("baseline:");
  Serial.print(baselineValue);
  Serial.print('\t');

  Serial.print("thrHigh:");
  Serial.print(thresholdHigh);
  Serial.print('\t');

  Serial.print("thrLow:");
  Serial.print(thresholdLow);
  Serial.print('\t');

  Serial.print("signalShift:");
  Serial.print(signalValue + 512);
  Serial.print('\t');

  Serial.print("sigRange:");
  Serial.println(signalRange);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 24, "A0 SERIAL PLOTTER");
    u8g2.drawStr(0, 42, "UPLOAD OK");
  } while (u8g2.nextPage());
  delay(700);
}

void loop() {
  updateSignal();
  emitPlotterLine();

  unsigned long now = millis();
  if (now - lastDisplayAt >= DISPLAY_INTERVAL_MS) {
    lastDisplayAt = now;
    drawDisplay();
  }

  delay(8);
}
