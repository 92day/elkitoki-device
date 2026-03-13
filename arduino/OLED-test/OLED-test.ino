#include <Wire.h>
#include <U8g2lib.h>

// 먼저 이 버전으로 테스트
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int redLedPin = 7;
const int greenLedPin = 2;

void drawStatus(const char* text) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(28, 22, "LED");
  u8g2.drawStr(18, 50, text);
  u8g2.sendBuffer();
}

void setup() {
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);

  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  Wire.begin();
  u8g2.begin();

  drawStatus("START");
  delay(1000);
}

void loop() {
  digitalWrite(redLedPin, HIGH);
  digitalWrite(greenLedPin, LOW);
  drawStatus("RED");
  delay(2000);

  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);
  drawStatus("GREEN");
  delay(2000);
}
