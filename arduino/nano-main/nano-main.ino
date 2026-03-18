#include <Arduino_LSM9DS1.h>

const int SOUND_A_PIN = A0;
const int SOUND_B_PIN = A1;
const int SOUND_C_PIN = A2;

const unsigned long STATUS_INTERVAL_MS = 500;
const unsigned long EVENT_COOLDOWN_MS = 2500;

const float FALL_TILT_THRESHOLD_DEG = 55.0f;
const int NOISE_CAUTION_SCORE = 40;
const int NOISE_DANGER_SCORE = 40;

unsigned long lastStatusAt = 0;
unsigned long lastTiltEventAt = 0;
unsigned long lastNoiseEventAtA = 0;
unsigned long lastNoiseEventAtB = 0;
unsigned long lastNoiseEventAtC = 0;

bool imuReady = false;
bool fallActive = false;
bool noiseAlertA = false;
bool noiseAlertB = false;
bool noiseAlertC = false;

int rawToNoiseScore(int rawValue) {
  const int clamped = constrain(rawValue, 0, 1023);
  const float normalized = clamped / 1023.0f;
  const float scaled = 1.0f + powf(normalized, 0.58f) * 99.0f;
  return constrain((int)(scaled + 0.5f), 1, 100);
}

void emitStatus(int soundA, int soundB, int soundC, float pitch, float roll, bool tiltAlert) {
  Serial.print("{\"kind\":\"status\",\"device\":\"nano-main\"");
  Serial.print(",\"soundA\":");
  Serial.print(soundA);
  Serial.print(",\"soundB\":");
  Serial.print(soundB);
  Serial.print(",\"soundC\":");
  Serial.print(soundC);
  Serial.print(",\"soundAlertA\":");
  Serial.print(noiseAlertA ? "true" : "false");
  Serial.print(",\"soundAlertB\":");
  Serial.print(noiseAlertB ? "true" : "false");
  Serial.print(",\"soundAlertC\":");
  Serial.print(noiseAlertC ? "true" : "false");
  Serial.print(",\"nanoConnected\":true");
  Serial.print(",\"pitch\":");
  Serial.print(pitch, 1);
  Serial.print(",\"roll\":");
  Serial.print(roll, 1);
  Serial.print(",\"tiltAlert\":");
  Serial.print(tiltAlert ? "true" : "false");
  Serial.println("}");
}

void emitNoiseEvent(const char* zone, int value) {
  Serial.print("{\"kind\":\"event\",\"device\":\"nano-main\",\"eventType\":\"noise_abnormal\",\"zone\":\"");
  Serial.print(zone);
  Serial.print("\",\"value\":");
  Serial.print(value);
  Serial.println(",\"active\":true}");
}

void emitFallEvent(float pitch, float roll) {
  Serial.print("{\"kind\":\"event\",\"device\":\"nano-main\",\"eventType\":\"fall_detected\",\"active\":true");
  Serial.print(",\"pitch\":");
  Serial.print(pitch, 1);
  Serial.print(",\"roll\":");
  Serial.print(roll, 1);
  Serial.println("}");
}

void updateNoiseAlerts(int soundA, int soundB, int soundC) {
  const unsigned long now = millis();

  noiseAlertA = soundA >= NOISE_CAUTION_SCORE;
  noiseAlertB = soundB >= NOISE_CAUTION_SCORE;
  noiseAlertC = soundC >= NOISE_CAUTION_SCORE;

  if (soundA >= NOISE_DANGER_SCORE && now - lastNoiseEventAtA >= EVENT_COOLDOWN_MS) {
    emitNoiseEvent("A", soundA);
    lastNoiseEventAtA = now;
  }

  if (soundB >= NOISE_DANGER_SCORE && now - lastNoiseEventAtB >= EVENT_COOLDOWN_MS) {
    emitNoiseEvent("B", soundB);
    lastNoiseEventAtB = now;
  }

  if (soundC >= NOISE_DANGER_SCORE && now - lastNoiseEventAtC >= EVENT_COOLDOWN_MS) {
    emitNoiseEvent("C", soundC);
    lastNoiseEventAtC = now;
  }
}

void updateTiltAlert(float pitch, float roll) {
  const unsigned long now = millis();
  const bool nextFallActive = abs(pitch) >= FALL_TILT_THRESHOLD_DEG || abs(roll) >= FALL_TILT_THRESHOLD_DEG;

  if (nextFallActive && !fallActive && now - lastTiltEventAt >= EVENT_COOLDOWN_MS) {
    emitFallEvent(pitch, roll);
    lastTiltEventAt = now;
  }

  fallActive = nextFallActive;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(10);

  imuReady = IMU.begin();
}

void loop() {
  float x = 0.0f;
  float y = 0.0f;
  float z = 1.0f;

  if (imuReady && IMU.accelerationAvailable()) {
    IMU.readAcceleration(x, y, z);
  }

  const float pitch = atan2f(x, sqrtf(y * y + z * z)) * 180.0f / PI;
  const float roll = atan2f(y, z) * 180.0f / PI;

  const int soundA = rawToNoiseScore(analogRead(SOUND_A_PIN));
  const int soundB = rawToNoiseScore(analogRead(SOUND_B_PIN));
  const int soundC = rawToNoiseScore(analogRead(SOUND_C_PIN));

  updateNoiseAlerts(soundA, soundB, soundC);
  updateTiltAlert(pitch, roll);

  const unsigned long now = millis();
  if (now - lastStatusAt >= STATUS_INTERVAL_MS) {
    emitStatus(soundA, soundB, soundC, pitch, roll, fallActive);
    lastStatusAt = now;
  }

  delay(30);
}




