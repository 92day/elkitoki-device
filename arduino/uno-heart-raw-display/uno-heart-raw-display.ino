#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int HEART_A_PIN = A0;
const int HEART_B_PIN = A1;
const unsigned long DISPLAY_INTERVAL_MS = 120;
const unsigned long SERIAL_INTERVAL_MS = 250;

int rawA = 0;
int rawB = 0;
int smoothA = 0;
int smoothB = 0;
bool initialized = false;

unsigned long lastDisplayAt = 0;
unsigned long lastSerialAt = 0;

void updateHeartRaw() {
  rawA = analogRead(HEART_A_PIN);
  rawB = analogRead(HEART_B_PIN);

  if (!initialized) {
    smoothA = rawA;
    smoothB = rawB;
    initialized = true;
    return;
  }

  smoothA = (smoothA * 3 + rawA) / 4;
  smoothB = (smoothB * 3 + rawB) / 4;
}

void drawDisplay() {
  char line1[24];
  char line2[24];
  char line3[24];
  char line4[24];

  snprintf(line1, sizeof(line1), "RED RAW  %4d", rawA);
  snprintf(line2, sizeof(line2), "RED AVG  %4d", smoothA);
  snprintf(line3, sizeof(line3), "GRN RAW  %4d", rawB);
  snprintf(line4, sizeof(line4), "GRN AVG  %4d", smoothB);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "HEART RAW MONITOR");
    u8g2.drawStr(0, 26, line1);
    u8g2.drawStr(0, 38, line2);
    u8g2.drawStr(0, 50, line3);
    u8g2.drawStr(0, 62, line4);
  } while (u8g2.nextPage());
}

void emitSerialLine() {
  Serial.print("redRaw:");
  Serial.print(rawA);
  Serial.print('\t');
  Serial.print("redAvg:");
  Serial.print(smoothA);
  Serial.print('\t');
  Serial.print("greenRaw:");
  Serial.print(rawB);
  Serial.print('\t');
  Serial.print("greenAvg:");
  Serial.println(smoothB);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 24, "HEART RAW MONITOR");
    u8g2.drawStr(0, 42, "UPLOAD OK");
  } while (u8g2.nextPage());
  delay(800);
}

void loop() {
  updateHeartRaw();

  unsigned long now = millis();
  if (now - lastDisplayAt >= DISPLAY_INTERVAL_MS) {
    lastDisplayAt = now;
    drawDisplay();
  }

  if (now - lastSerialAt >= SERIAL_INTERVAL_MS) {
    lastSerialAt = now;
    emitSerialLine();
  }

  delay(20);
}
