#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 9);

const int redLedPin = 7;
const int greenLedPin = 2;
const int pulsePin = A0;

void drawStatus(const char* ledText, int pulseValue, const char* fingerText) {
  char line[20];

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);

    u8g2.drawStr(0, 12, "LED:");
    u8g2.drawStr(35, 12, ledText);

    sprintf(line, "RAW: %d", pulseValue);
    u8g2.drawStr(0, 32, line);

    u8g2.drawStr(0, 52, fingerText);
  } while (u8g2.nextPage());
}

void setup() {
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);

  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  Serial.begin(115200);
  Wire.begin();
  u8g2.begin();

  drawStatus("START", 0, "READY");
  delay(1000);
}

void loop() {
  int pulseValue = analogRead(pulsePin);
  const char* fingerText = (pulseValue > 500) ? "FINGER OK" : "NO FINGER";

  digitalWrite(redLedPin, HIGH);
  digitalWrite(greenLedPin, LOW);
  drawStatus("RED", pulseValue, fingerText);
  delay(2000);

  pulseValue = analogRead(pulsePin);
  fingerText = (pulseValue > 500) ? "FINGER OK" : "NO FINGER";

  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);
  drawStatus("GREEN", pulseValue, fingerText);
  delay(2000);
}
