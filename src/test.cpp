#include <Arduino.h>

// ===================== 感測器（AO） =====================
const int PIN_AO_L = 34;
const int PIN_AO_C = 35;
const int PIN_AO_R = 32;

// ===================== 前輪驅動 =====================
const int PIN_F_IN2 = 33;   // 右前
const int PIN_F_IN3 = 25;   // 左前
const int PIN_F_EN  = 26;   // 前輪 PWM

// ===================== 後輪驅動 =====================
const int PIN_B_IN2 = 27;   // 左後
const int PIN_B_IN3 = 17;   // 右後
const int PIN_B_EN  = 12;   // 後輪 PWM

// ===================== PWM =====================
const int PWM_FREQ = 1000;
const int PWM_RES  = 8;     // 0~255
const int CH_F_EN  = 0;
const int CH_B_EN  = 1;

// ===================== 速度參數 =====================
int baseSpeed   = 145;
int turnSpeed   = 175;
int searchSpeed = 115;

// 你目前車子實測：LOW=ON
const bool MOTOR_ACTIVE_HIGH = false;

// 黑線判斷：你實測黑線 AO > 1000、白底 < 400
int thL = 700;
int thC = 700;
int thR = 700;

enum LastTurn { LT_NONE, LT_LEFT, LT_RIGHT };
LastTurn lastTurn = LT_NONE;

// ===================== 工具函式 =====================
void writeMotorPin(int pin, bool on) {
  digitalWrite(pin, (on == MOTOR_ACTIVE_HIGH) ? HIGH : LOW);
}

void setFrontWheels(bool rightOn, bool leftOn) {
  writeMotorPin(PIN_F_IN2, rightOn); // 右前
  writeMotorPin(PIN_F_IN3, leftOn);  // 左前
}

void setBackWheels(bool leftOn, bool rightOn) {
  writeMotorPin(PIN_B_IN2, leftOn);  // 左後
  writeMotorPin(PIN_B_IN3, rightOn); // 右後
}

void setSpeed(int f, int b) {
  ledcWrite(CH_F_EN, constrain(f, 0, 255));
  ledcWrite(CH_B_EN, constrain(b, 0, 255));
}

int readAvg(int pin, int samples = 10) {
  long s = 0;
  for (int i = 0; i < samples; i++) {
    s += analogRead(pin);
    delayMicroseconds(200);
  }
  return (int)(s / samples);
}

bool isBlack(int value, int threshold) {
  return (value > threshold);
}

// ===================== 動作 =====================
void stopCar() {
  setFrontWheels(true, true);
  setBackWheels(true, true);
  setSpeed(0, 0);
}

void goStraight() {
  // 四輪都動
  setFrontWheels(false, false);
  setBackWheels(false, false);
  setSpeed(baseSpeed, baseSpeed);
}

void turnLeftHard() {
  // 停左側，右側前進
  setFrontWheels(false, true);  // 右前ON 左前OFF
  setBackWheels(true, false);   // 左後OFF 右後ON
  setSpeed(turnSpeed, turnSpeed);
}

void turnRightHard() {
  // 停右側，左側前進
  setFrontWheels(true, false);  // 右前OFF 左前ON
  setBackWheels(false, true);   // 左後ON 右後OFF
  setSpeed(turnSpeed, turnSpeed);
}

void searchLine() {
  if (lastTurn == LT_LEFT) turnLeftHard();
  else                     turnRightHard();
  setSpeed(searchSpeed, searchSpeed);
}

// ===================== Setup / Loop =====================
void setup() {
  Serial.begin(115200);

  analogReadResolution(12);        // 0~4095
  analogSetAttenuation(ADC_11db);  // 適合3.3V

  pinMode(PIN_F_IN2, OUTPUT);
  pinMode(PIN_F_IN3, OUTPUT);
  pinMode(PIN_B_IN2, OUTPUT);
  pinMode(PIN_B_IN3, OUTPUT);

  ledcSetup(CH_F_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_F_EN, CH_F_EN);
  ledcSetup(CH_B_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_B_EN, CH_B_EN);

  stopCar();
  delay(300);
}

void loop() {
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
  Serial.println(R);

  // ===== 中間優先：只要中間在黑線上就直行 =====
  if (C) {
    goStraight();   // 四輪全動
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
    // 中間沒看到但兩側看到，多半是交界，先直行
    goStraight();
  }
  else {
    // 全部沒看到
    searchLine();
  }

  delay(8);
}