#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

// ===== WiFi =====
const char* ssid = "柑橘的iphone3.0";
const char* pass = "%%%%%%%%";

// WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

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

// ===== 低延遲控制參數 =====
volatile int baseSpeed = 160;       // 0~255
volatile int throttle = 0;          // -1,0,1
volatile int steer = 0;             // -1,0,1
volatile uint16_t lastSeq = 0;      // 去重/丟舊包
volatile uint32_t lastCmdMs = 0;    // 失聯保護
const uint32_t CMD_TIMEOUT_MS = 150; // >150ms無新包就停車

// 控制迴圈節拍（固定輸出）
uint32_t lastControlTick = 0;
const uint32_t CONTROL_PERIOD_MS = 10; // 100Hz 控制輸出

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
  // 依你原邏輯：全 ON + speed 0 停車
  setFrontWheels(true, true);
  setBackWheels(true, true);
  setSpeed(0, 0);
}

void applyState(int t, int s, int spd) {
  // t: -1/0/1, s: -1/0/1
  if (t == 0 && s == 0) {
    stopCar();
    return;
  }

  int fSpeed = spd;
  int bSpeed = spd;

  if (t > 0) { // forward
    if (s < 0) { // left
      setFrontWheels(false, true);
      setBackWheels(true, false);
    } else if (s > 0) { // right
      setFrontWheels(true, false);
      setBackWheels(false, true);
    } else {
      setFrontWheels(false, false);
      setBackWheels(false, false);
    }
  } else if (t < 0) { // backward
    if (s < 0) { // backward-left
      setFrontWheels(false, true);
      setBackWheels(true, false);
    } else if (s > 0) { // backward-right
      setFrontWheels(true, false);
      setBackWheels(false, true);
    } else {
      setFrontWheels(true, true);
      setBackWheels(true, true);
    }
  } else { // t == 0, 只轉向（原地轉）
    if (s < 0) {
      setFrontWheels(false, true);
      setBackWheels(true, false);
    } else if (s > 0) {
      setFrontWheels(true, false);
      setBackWheels(false, true);
    } else {
      stopCar();
      return;
    }
  }

  setSpeed(fSpeed, bSpeed);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] Client #%u connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      webSocket.sendTXT(num, "OK CONNECTED");
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", num);
      throttle = 0; steer = 0;
      stopCar();
      break;

    case WStype_BIN: {
      if (length != sizeof(ControlPacket)) return;
      ControlPacket pkt;
      memcpy(&pkt, payload, sizeof(ControlPacket));

      // 丟棄舊包（簡單比較，若你跑超久可改成帶回繞比較）
      if (pkt.seq <= lastSeq) return;
      lastSeq = pkt.seq;

      int t = (pkt.throttle < 0) ? -1 : (pkt.throttle > 0 ? 1 : 0);
      int s = (pkt.steer < 0) ? -1 : (pkt.steer > 0 ? 1 : 0);

      throttle = t;
      steer = s;
      baseSpeed = constrain((int)pkt.speed, 0, 255);
      lastCmdMs = millis();
      break;
    }

    case WStype_TEXT: {
      // 相容文字命令（可保留）
      if (length == 0) return;
      char c = (char)payload[0];
      if (c == 'S') { throttle = 0; steer = 0; lastCmdMs = millis(); }
      break;
    }

    default:
      break;
  }
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

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // 關省電，降低延遲抖動（關鍵）
  WiFi.begin(ssid, pass);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(150);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  lastCmdMs = millis();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("[WS] server started on :81");
}

void loop() {
  webSocket.loop();

  uint32_t now = millis();

  // 失聯保護
  if (now - lastCmdMs > CMD_TIMEOUT_MS) {
    throttle = 0;
    steer = 0;
    stopCar();
  }

  // 固定控制頻率，輸出更平滑一致
  if (now - lastControlTick >= CONTROL_PERIOD_MS) {
    lastControlTick = now;
    applyState(throttle, steer, baseSpeed);
  }
}