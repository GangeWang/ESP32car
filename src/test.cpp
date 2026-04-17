#include <Arduino.h>

// ===================== 感測器（AO） =====================
const int PIN_AO_L = 34;
const int PIN_AO_C = 35;
const int PIN_AO_R = 32;

// ===================== 超聲波（HC-SR04） =====================
const int PIN_US_TRIG = 23;           // 你可改腳位
const int PIN_US_ECHO = 22;           // 你可改腳位
const int STOP_DIST_CM = 10;          // <= 15cm 停車
const unsigned long US_TIMEOUT = 30000UL; // 30ms timeout

// ===================== 前輪驅動 =====================
const int PIN_F_IN2 = 33;   // 右前
const int PIN_F_IN3 = 25;   // 左前
const int PIN_F_EN  = 26;   // 前輪 PWM

// ===================== 後輪驅動 =====================
const int PIN_B_IN2 = 27;   // 左後
const int PIN_B_IN3 = 17;   // 右後
const int PIN_B_EN  = 12;   // 後輪 PWM

// ===================== PWM =====================
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;     // 0~255
const int CH_F_EN  = 0;
const int CH_B_EN  = 1;

// ===================== 速度參數 =====================
int baseSpeed   = 235;
int turnSpeed   = 245;
int searchSpeed = 210;

// 你目前車子實測：LOW=ON
const bool MOTOR_ACTIVE_HIGH = false;

// 黑線判斷
int thL = 300;
int thC = 300;
int thR = 300;

// ===================== 🔥 計時功能 =====================
unsigned long startTime = 0;
const unsigned long LIMIT_TIME = 18000; // 18秒

enum LastTurn { LT_NONE, LT_LEFT, LT_RIGHT };
LastTurn lastTurn = LT_NONE;

// ===================== 工具函式 =====================
void writeMotorPin(int pin, bool on) {
  digitalWrite(pin, (on == MOTOR_ACTIVE_HIGH) ? HIGH : LOW);
}

void setFrontWheels(bool rightOn, bool leftOn) {
  writeMotorPin(PIN_F_IN2, rightOn);
  writeMotorPin(PIN_F_IN3, leftOn);
}

void setBackWheels(bool leftOn, bool rightOn) {
  writeMotorPin(PIN_B_IN2, leftOn);
  writeMotorPin(PIN_B_IN3, rightOn);
}

void setSpeed(int f, int b) {
  ledcWrite(CH_F_EN, constrain(f, 0, 255));
  ledcWrite(CH_B_EN, constrain(b, 0, 255));
}

int readAvg(int pin, int samples = 4) {
  analogRead(pin); // dummy read（降低ESP32 ADC首筆飄動）
  long s = 0;
  for (int i = 0; i < samples; i++) {
    s += analogRead(pin);
    delayMicroseconds(60);
  }
  return (int)(s / samples);
}

bool isBlack(int value, int threshold) {
  return (value > threshold);
}

float readDistanceCM() {
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_US_ECHO, HIGH, US_TIMEOUT);
  if (duration == 0) return 999.0f; // timeout 視為很遠

  return (duration * 0.0343f) / 2.0f;
}

// ===================== 動作 =====================
void stopCar() {
  setFrontWheels(true, true);
  setBackWheels(true, true);
  setSpeed(0, 0);
}

void goStraight() {
  setFrontWheels(false, false);
  setBackWheels(false, false);
  setSpeed(baseSpeed, baseSpeed);
}

void turnLeftHard() {
  setFrontWheels(false, true);
  setBackWheels(true, false);
  setSpeed(turnSpeed, turnSpeed);
}

void turnRightHard() {
  setFrontWheels(true, false);
  setBackWheels(false, true);
  setSpeed(turnSpeed, turnSpeed);
}

void searchLine() {
  if (lastTurn == LT_LEFT) turnLeftHard();
  else                     turnRightHard();
  setSpeed(searchSpeed, searchSpeed);
}

void all_stop() {
  setFrontWheels(true, true);
  setBackWheels(true, true);
  setSpeed(0, 0);
}

// ===================== Setup / Loop =====================
void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_F_IN2, OUTPUT);
  pinMode(PIN_F_IN3, OUTPUT);
  pinMode(PIN_B_IN2, OUTPUT);
  pinMode(PIN_B_IN3, OUTPUT);

  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);
  digitalWrite(PIN_US_TRIG, LOW);

  ledcSetup(CH_F_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_F_EN, CH_F_EN);
  ledcSetup(CH_B_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_B_EN, CH_B_EN);

  stopCar();
  delay(300);

  // 🔥 開始計時
  startTime = millis();
}

void loop() {
  // ===== 超音波防撞優先 =====
  float d = readDistanceCM();
  if (d <= STOP_DIST_CM) {
    all_stop();
    Serial.print("ULTRASONIC STOP, d=");
    Serial.println(d);
    delay(30);
    return;
  }

  int vL = readAvg(PIN_AO_L);
  int vC = readAvg(PIN_AO_C);
  int vR = readAvg(PIN_AO_R);

  bool L = isBlack(vL, thL);
  bool C = isBlack(vC, thC);
  bool R = isBlack(vR, thR);

  Serial.print("AO ");
  Serial.print(vL); Serial.print("/");
  Serial.print(vC); Serial.print("/");
  Serial.print(vR);
  Serial.print(" -> B ");
  Serial.print(L); Serial.print(" ");
  Serial.print(C); Serial.print(" ");
  Serial.print(R);
  Serial.print(" | US ");
  Serial.println(d);

  // ===== 中間優先 =====
  if (C) {
    goStraight();
  }
  else if (L && !R) {
    turnLeftHard();
    lastTurn = LT_LEFT;
  }
  else if (R && !L) {
    turnRightHard();
    lastTurn = LT_RIGHT;
  }
  else if (L && R) {
    goStraight();
  }
  else {
    // ===== 🔥 18秒後停止功能 =====
    if (millis() - startTime >= LIMIT_TIME) {
      all_stop();
    } else {
      searchLine();
    }
  }

  delay(2);
}