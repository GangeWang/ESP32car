#include <Arduino.h>
#include <NimBLEDevice.h>

// ===== BLE UUID =====
static NimBLEUUID SERVICE_UUID("0000FFE0-0000-1000-8000-00805F9B34FB");
static NimBLEUUID CHAR_UUID   ("0000FFE1-0000-1000-8000-00805F9B34FB");

// ===== 前輪驅動 =====
const int PIN_F_IN2 = 33;   // 右前
const int PIN_F_IN3 = 25;   // 左前
const int PIN_F_EN  = 26;   // 前輪 PWM

// ===== 後輪驅動 =====
const int PIN_B_IN2 = 27;   // 左後
const int PIN_B_IN3 = 17;   // 右後
const int PIN_B_EN  = 12;   // 後輪 PWM

// ===== PWM =====
const int PWM_FREQ = 1000;
const int PWM_RES  = 8;
const int CH_F_EN  = 0;
const int CH_B_EN  = 1;

// 你的車：LOW=ON
const bool MOTOR_ACTIVE_HIGH = false;

// ===== 控制狀態 =====
volatile int baseSpeed = 160;       // 0~255
volatile int throttle = 0;          // -1,0,1
volatile int steer = 0;             // -1,0,1
volatile uint16_t lastSeq = 0;      // 去重
volatile uint32_t lastCmdMs = 0;
const uint32_t CMD_TIMEOUT_MS = 200;

uint32_t lastControlTick = 0;
const uint32_t CONTROL_PERIOD_MS = 10;

// 廣播保活檢查
uint32_t lastAdvCheckMs = 0;

// 文字命令緩衝
String bleLine = "";

#pragma pack(push,1)
struct ControlPacket {
  uint16_t seq;     // little-endian
  int8_t throttle;  // -1 back, 0 stop, 1 forward
  int8_t steer;     // -1 left, 0 straight, 1 right
  uint8_t speed;    // 0~255
};
#pragma pack(pop)

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
void stopCar() {
  setFrontWheels(true, true);
  setBackWheels(true, true);
  setSpeed(0, 0);
}

void applyState(int t, int s, int spd) {
  if (t == 0 && s == 0) {
    stopCar();
    return;
  }

  if (t > 0) { // forward
    if (s < 0)      { setFrontWheels(false, true);  setBackWheels(true, false); }
    else if (s > 0) { setFrontWheels(true, false);  setBackWheels(false, true); }
    else            { setFrontWheels(false, false); setBackWheels(false, false); }
  } else if (t < 0) { // backward
    if (s < 0)      { setFrontWheels(false, true);  setBackWheels(true, false); }
    else if (s > 0) { setFrontWheels(true, false);  setBackWheels(false, true); }
    else            { setFrontWheels(true, true);   setBackWheels(true, true); }
  } else { // t == 0 原地轉
    if (s < 0)      { setFrontWheels(false, true);  setBackWheels(true, false); }
    else if (s > 0) { setFrontWheels(true, false);  setBackWheels(false, true); }
    else            { stopCar(); return; }
  }

  setSpeed(spd, spd);
}

void applyPacket(const ControlPacket& pkt) {
  if (pkt.seq <= lastSeq) return;
  lastSeq = pkt.seq;

  throttle = (pkt.throttle < 0) ? -1 : (pkt.throttle > 0 ? 1 : 0);
  steer    = (pkt.steer < 0) ? -1 : (pkt.steer > 0 ? 1 : 0);
  baseSpeed = constrain((int)pkt.speed, 0, 255);
  lastCmdMs = millis();
}

void handleTextCmd(const String& line) {
  if (line.isEmpty()) return;
  char c = line[0];

  if (c == 'F')      { throttle = 1;  steer = 0; lastCmdMs = millis(); }
  else if (c == 'B') { throttle = -1; steer = 0; lastCmdMs = millis(); }
  else if (c == 'L') { steer = -1; lastCmdMs = millis(); }
  else if (c == 'R') { steer = 1;  lastCmdMs = millis(); }
  else if (c == 'S') { throttle = 0; steer = 0; stopCar(); lastCmdMs = millis(); }
  else if (c == 'V') {
    int v = line.substring(1).toInt(); // V180
    baseSpeed = constrain(v, 0, 255);
    lastCmdMs = millis();
  }
}

class CarCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch) override {
    std::string v = ch->getValue();
    size_t len = v.size();

    // 二進位封包
    if (len == sizeof(ControlPacket)) {
      ControlPacket pkt;
      memcpy(&pkt, v.data(), sizeof(ControlPacket));
      applyPacket(pkt);
      return;
    }

    // 文字命令（可多行）
    for (size_t i = 0; i < len; ++i) {
      char c = (char)v[i];
      if (c == '\n' || c == '\r') {
        bleLine.trim();
        if (!bleLine.isEmpty()) handleTextCmd(bleLine);
        bleLine = "";
      } else {
        bleLine += c;
      }
    }
  }
};

void setupBLE() {
  NimBLEDevice::init("ESP32car-BLE");
  NimBLEDevice::setDeviceName("ESP32car-BLE");

  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* svc = server->createService("FFE0");

  NimBLECharacteristic* ch = svc->createCharacteristic(
    "FFE1",
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  ch->setCallbacks(new CarCharCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();

  // 關鍵：iOS 相容性
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06); // iPhone/iPad 常見建議值
  adv->setMinPreferred(0x12);

  adv->addServiceUUID("FFE0");
  adv->start();

  Serial.println("[BLE] advertising start");
}
void setup() {
  Serial.begin(115200);

  pinMode(PIN_F_IN2, OUTPUT);
  pinMode(PIN_F_IN3, OUTPUT);
  pinMode(PIN_B_IN2, OUTPUT);
  pinMode(PIN_B_IN3, OUTPUT);

  ledcSetup(CH_F_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_F_EN, CH_F_EN);
  ledcSetup(CH_B_EN, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_B_EN, CH_B_EN);

  stopCar();
  setupBLE();
  lastCmdMs = millis();
}

void loop() {
  uint32_t now = millis();

  // 失聯保護
  if (now - lastCmdMs > CMD_TIMEOUT_MS) {
    throttle = 0;
    steer = 0;
    stopCar();
  }

  // 固定控制頻率
  if (now - lastControlTick >= CONTROL_PERIOD_MS) {
    lastControlTick = now;
    applyState(throttle, steer, baseSpeed);
  }

  // 廣播保活（每 3 秒檢查一次）
  if (now - lastAdvCheckMs > 3000) {
    lastAdvCheckMs = now;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv->isAdvertising()) {
      bool ok = adv->start();
      Serial.printf("[BLE] advertising restart: %s\n", ok ? "OK" : "FAIL");
    }
  }
}