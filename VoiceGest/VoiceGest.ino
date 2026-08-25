#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <BluetoothSerial.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_APDS9960.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <vector>
#include <memory>

const char* WIFI_SSID = "VoiceGest";
// NOTE: leave this truly empty and DO NOT pass it to softAP() below.
// Passing "" as a password argument to WiFi.softAP() is what was
// causing the boot-loop / unreachable AP.
const char* WIFI_PASSWORD = "";

#define BUZZER_PIN 12
#define OLED_RESET_PIN -1
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define MPU_ADDR_LOW  0x68
#define MPU_ADDR_HIGH 0x69
#define OLED_ADDR 0x3C
#define MPU_REG_WHO_AM_I     0x75
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_SMPLRT_DIV   0x19
#define MPU_REG_CONFIG       0x1A
#define MPU_REG_GYRO_CONFIG  0x1B
#define MPU_REG_ACCEL_CONFIG 0x1C
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define TILT_THRESHOLD 4.0
#define SHAKE_THRESHOLD 14.0
#define GYRO_ROTATE_THRESHOLD 80.0
#define DOUBLE_TAP_WINDOW 600
#define DOUBLE_TAP_ACCEL 18.0
#define PROXIMITY_GATE 10
#define PRESSURE_DELTA_MIN 0.5
#define STABILITY_WINDOW 300
#define CONFIRM_HOLD_MS 1000
#define SENSOR_POLL_MS 10
#define OLED_REFRESH_MS 150
#define LOG_FILE "/log.json"
#define MAX_LOG_LINES 10
#define MAX_LOG_FILE_BYTES 20000   // rotate log before it grows unbounded

Adafruit_BMP085_Unified bmp(10085);
Adafruit_APDS9960 apds;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
BluetoothSerial SerialBT;
AsyncWebServer server(80);

bool mpuAvailable = false;
bool bmpAvailable = false;
bool apdsAvailable = false;
uint8_t mpuAddr = 0;

float accelX, accelY, accelZ;
float gyroX, gyroY, gyroZ;
float currentPressure = 0.0;
float basePressure = 0.0;
bool breathDetected = false;

int totalEvents = 0;
unsigned long lastSensorPoll = 0;
unsigned long lastOledUpdate = 0;
unsigned long bootTime = 0;

unsigned long lastTapTime = 0;
int tapCount = 0;

String pendingPhrase = "";
unsigned long pendingSince = 0;
bool phrasePending = false;

String lastSpokenPhrase = "";
unsigned long lastSpokenTime = 0;

String getTimestamp() {
  unsigned long sec = (millis() - bootTime) / 1000;
  int h = sec / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

String getUptime() { return getTimestamp(); }

void buzz(int ms = 150, int times = 1) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ms);
    digitalWrite(BUZZER_PIN, LOW);
    if (times > 1) delay(100);
  }
}

// Rotate the log file if it's grown too large, keeping only the tail.
// This is what was silently ballooning buildDashboard()'s memory use
// over a multi-hour test session.
void rotateLogIfNeeded() {
  File f = SPIFFS.open(LOG_FILE, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  if (sz <= MAX_LOG_FILE_BYTES) { f.close(); return; }

  std::vector<String> lines;
  while (f.available()) {
    String ln = f.readStringUntil('\n');
    ln.trim();
    if (ln.length() > 2) lines.push_back(ln);
  }
  f.close();

  int start = max(0, (int)lines.size() - MAX_LOG_LINES);
  File out = SPIFFS.open(LOG_FILE, FILE_WRITE);
  if (!out) return;
  for (int i = start; i < (int)lines.size(); i++) {
    out.println(lines[i]);
  }
  out.close();
}

void logEvent(String type, String detail) {
  totalEvents++;
  StaticJsonDocument<256> doc;
  doc["t"] = getTimestamp();
  doc["e"] = type;
  doc["d"] = detail;
  doc["n"] = totalEvents;
  String line;
  serializeJson(doc, line);
  File f = SPIFFS.open(LOG_FILE, FILE_APPEND);
  if (f) { f.println(line); f.close(); }
  Serial.println("[LOG] " + line);

  rotateLogIfNeeded();
}

void speakPhrase(String phrase) {
  SerialBT.println(phrase);
  Serial.println("[SPEAK] " + phrase);
  logEvent("PHRASE", phrase);
  buzz(150, 1);
  lastSpokenPhrase = phrase;
  lastSpokenTime = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SPOKEN:");
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(phrase);
  display.display();
}

void proposePhrase(String phrase) {
  if (phrasePending && pendingPhrase == phrase) return;
  pendingPhrase = phrase;
  pendingSince = millis();
  phrasePending = true;
}

void cancelPending(const char* reason) {
  if (phrasePending) {
    buzz(120, 2);
    logEvent("CANCELLED", pendingPhrase + " (" + reason + ")");
  }
  phrasePending = false;
  pendingPhrase = "";
}

void scanI2CBus() {
  Serial.println("[I2C] Scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("[I2C]   Device found at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("[I2C]   No devices found at all - check SDA/SCL wiring and power.");
  } else {
    Serial.printf("[I2C] Scan complete: %d device(s) found.\n", found);
  }
}

bool mpuWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool mpuReadBytes(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((int)addr, (int)len);
  if (got != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool initMPU6050Raw() {
  uint8_t candidates[2] = { MPU_ADDR_LOW, MPU_ADDR_HIGH };

  for (uint8_t i = 0; i < 2; i++) {
    uint8_t addr = candidates[i];

    bool acked = false;
    for (uint8_t attempt = 0; attempt < 5 && !acked; attempt++) {
      Wire.beginTransmission(addr);
      uint8_t err = Wire.endTransmission();
      if (err == 0) {
        acked = true;
      } else {
        Serial.printf("[MPU] 0x%02X ack attempt %d failed, err=%d\n", addr, attempt, err);
        delay(20);
      }
    }
    if (!acked) continue;

    uint8_t whoami = 0xFF;
    mpuReadBytes(addr, MPU_REG_WHO_AM_I, &whoami, 1);
    Serial.printf("[MPU] Device acked at 0x%02X, WHO_AM_I = 0x%02X\n", addr, whoami);

    mpuWriteReg(addr, MPU_REG_PWR_MGMT_1, 0x00);
    delay(100);
    mpuWriteReg(addr, MPU_REG_SMPLRT_DIV, 0x07);
    mpuWriteReg(addr, MPU_REG_CONFIG, 0x03);
    mpuWriteReg(addr, MPU_REG_GYRO_CONFIG, 0x08);
    mpuWriteReg(addr, MPU_REG_ACCEL_CONFIG, 0x10);
    delay(20);

    uint8_t raw[14];
    if (mpuReadBytes(addr, MPU_REG_ACCEL_XOUT_H, raw, 14)) {
      mpuAddr = addr;
      Serial.printf("[OK] MPU6050 ready at 0x%02X (raw driver)\n", addr);
      return true;
    } else {
      Serial.printf("[MPU] 0x%02X acked but 14-byte burst read failed\n", addr);
    }
  }
  return false;
}

void readMPURaw() {
  uint8_t raw[14];
  if (!mpuReadBytes(mpuAddr, MPU_REG_ACCEL_XOUT_H, raw, 14)) return;

  int16_t ax = (raw[0] << 8) | raw[1];
  int16_t ay = (raw[2] << 8) | raw[3];
  int16_t az = (raw[4] << 8) | raw[5];
  int16_t gx = (raw[8] << 8) | raw[9];
  int16_t gy = (raw[10] << 8) | raw[11];
  int16_t gz = (raw[12] << 8) | raw[13];

  accelX = (ax / 4096.0) * 9.80665;
  accelY = (ay / 4096.0) * 9.80665;
  accelZ = (az / 4096.0) * 9.80665;

  gyroX = gx / 65.5;
  gyroY = gy / 65.5;
  gyroZ = gz / 65.5;
}

void readAllSensors() {
  if (mpuAvailable) {
    readMPURaw();
  }

  if (bmpAvailable) {
    sensors_event_t ev;
    bmp.getEvent(&ev);
    if (ev.pressure) {
      currentPressure = ev.pressure;
      if (basePressure == 0) basePressure = currentPressure;
    }
  }
}

String classifyMPU() {
  if (!mpuAvailable) return "";

  float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

  if (mag > DOUBLE_TAP_ACCEL) {
    unsigned long now = millis();
    if (now - lastTapTime < DOUBLE_TAP_WINDOW) {
      tapCount++;
      if (tapCount >= 2) {
        tapCount = 0;
        return "EMERGENCY";
      }
    } else {
      tapCount = 1;
    }
    lastTapTime = now;
  }

  if (mag > SHAKE_THRESHOLD && mag <= DOUBLE_TAP_ACCEL) {
    return "YES";
  }

  if (gyroZ > GYRO_ROTATE_THRESHOLD) {
    return "NO";
  }
  if (gyroZ < -GYRO_ROTATE_THRESHOLD) {
    return "Thank you";
  }

  if (accelX > TILT_THRESHOLD)  return "I need help";
  if (accelX < -TILT_THRESHOLD) return "I am hungry";
  if (accelY > TILT_THRESHOLD)  return "I am in pain";
  if (accelY < -TILT_THRESHOLD) return "I need water";

  return "";
}

String classifyAPDSGesture() {
  if (!apdsAvailable) return "";
  uint8_t g = apds.readGesture();
  switch (g) {
    case APDS9960_UP:    return "Call my doctor";
    case APDS9960_DOWN:  return "I want to go home";
    case APDS9960_LEFT:  return "I feel good";
    case APDS9960_RIGHT: return "I need medicine";
    default: return "";
  }
}

String classifyColorCard() {
  if (!apdsAvailable) return "";
  if (!apds.colorDataReady()) return "";
  uint16_t r, g, b, c;
  apds.getColorData(&r, &g, &b, &c);
  if (c < 50) return "";

  if (r > g && r > b && r > 150)      return "Emergency, call an ambulance";
  else if (g > r && g > b && g > 150) return "I feel okay";
  else if (b > r && b > g && b > 150) return "I want to rest";
  else if (r > 120 && g > 120 && b < 100) return "I am happy";

  return "";
}

bool checkProximityGate() {
  if (!apdsAvailable) return true;
  apds.enableProximity(true);
  uint8_t prox = apds.readProximity();
  return prox > (255 - PROXIMITY_GATE * 10);
}

bool checkBreath() {
  if (!bmpAvailable || basePressure == 0) return false;
  float delta = currentPressure - basePressure;
  if (delta > PRESSURE_DELTA_MIN) {
    basePressure = currentPressure;
    return true;
  }
  basePressure = basePressure * 0.98 + currentPressure * 0.02;
  return false;
}

void runGestureEngine() {
  if (phrasePending) {
    float mag = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
    bool stillMoving = (fabs(mag - 9.8) > 2.0) || (fabs(gyroZ) > 15.0);

    if (stillMoving) {
      pendingSince = millis();
    }

    if (millis() - pendingSince >= CONFIRM_HOLD_MS) {
      String phrase = pendingPhrase;
      bool breath = checkBreath();
      if (breath) phrase = "PLEASE, " + phrase;
      speakPhrase(phrase);
      phrasePending = false;
      pendingPhrase = "";
    }
    return;
  }

  bool gateOpen = checkProximityGate();

  String phrase = "";

  phrase = classifyMPU();

  if (phrase == "" && gateOpen) {
    phrase = classifyAPDSGesture();
  }

  if (phrase == "" && gateOpen) {
    phrase = classifyColorCard();
  }

  if (phrase != "") {
    proposePhrase(phrase);
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (phrasePending) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Hold still to confirm:");
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println(pendingPhrase);
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print("Move to cancel");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("VOICEGEST | Ready");
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    display.setCursor(0, 16);
    display.print("Events: "); display.println(totalEvents);
    display.setCursor(0, 28);
    if (lastSpokenPhrase != "") {
      display.println("Last said:");
      display.setCursor(0, 40);
      display.println(lastSpokenPhrase);
    }
    display.setCursor(0, 56);
    display.print("WiFi: 192.168.4.1");
  }

  display.display();
}

// ---- Dashboard: streamed instead of built as one giant String ----
// This is the main reboot fix. The old version concatenated the full
// HTML page (head + table rows from the log file + script) into a
// single String on the async web server's task stack. As the log grew
// over a multi-hour session, that allocation grew past what the task
// could handle and crashed the whole board. Streaming it out in small
// chunks keeps peak memory tiny regardless of log size.

String buildLogRowsOnly() {
  String rows = "";
  File f = SPIFFS.open(LOG_FILE, FILE_READ);
  if (f) {
    std::vector<String> lines;
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      ln.trim();
      if (ln.length() > 2) lines.push_back(ln);
    }
    f.close();
    int start = max(0, (int)lines.size() - MAX_LOG_LINES);
    for (int i = (int)lines.size() - 1; i >= start; i--) {
      StaticJsonDocument<200> doc;
      if (!deserializeJson(doc, lines[i])) {
        rows += "<tr><td>" + doc["t"].as<String>() + "</td><td>" + doc["e"].as<String>() + "</td><td>" + doc["d"].as<String>() + "</td></tr>";
      }
    }
  }
  if (rows == "") rows = "<tr><td colspan='3'>No events yet</td></tr>";
  return rows;
}

String buildFullDashboardHTML() {
  String h;
  h.reserve(2500);
  h += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>VoiceGest</title><style>";
  h += "body{font-family:Arial;background:#0d0d1a;color:#e0e0e0;padding:10px;max-width:480px;margin:auto}";
  h += "h1{font-size:16px;text-align:center}";
  h += "table{width:100%;font-size:11px;border-collapse:collapse;margin-top:10px}";
  h += "td,th{padding:5px;border-bottom:1px solid #333}";
  h += "button{display:block;margin:10px auto;padding:8px 16px;background:#2a2a45;color:#fff;border:none;border-radius:6px}";
  h += "</style></head><body>";
  h += "<h1>VOICEGEST Dashboard</h1>";
  h += "<p id='status' style='text-align:center;font-size:11px;color:#4ade80'>Tap Enable Speech</p>";
  h += "<button id='btn' onclick='en()'>Enable Speech</button>";
  h += "<table id='log'><tr><th>Time</th><th>Event</th><th>Detail</th></tr>";
  h += buildLogRowsOnly();
  h += "</table>";
  h += "<div style='text-align:center;font-size:10px;color:#666;margin-top:10px'>Events: " + String(totalEvents) + "</div>";
  h += "<script>";
  h += "let lp='',en2=false;";
  h += "function en(){var u=new SpeechSynthesisUtterance('ready');speechSynthesis.speak(u);en2=true;";
  h += "document.getElementById('status').innerText='Voice: ON';document.getElementById('btn').style.display='none';}";
  h += "function sp(t){if(!en2)return;var u=new SpeechSynthesisUtterance(t);speechSynthesis.speak(u);}";
  h += "setInterval(function(){fetch('/last').then(r=>r.text()).then(t=>{if(t&&t!==lp){lp=t;sp(t);}});},1000);";
  h += "</script></body></html>";
  return h;
}

// A chunked response is the correct way to serve dynamically-sized
// content in ESPAsyncWebServer. Unlike AsyncResponseStream (which has
// a small fixed internal buffer that overflows and aborts the
// connection if you push content into it faster than TCP can drain
// it - exactly what ERR_CONNECTION_ABORTED was), a chunked response
// only hands over as many bytes as the library asks for, each time
// it asks, respecting TCP backpressure automatically.
void handleDashboard(AsyncWebServerRequest* req) {
  auto html = std::make_shared<String>(buildFullDashboardHTML());
  auto offset = std::make_shared<size_t>(0);

  AsyncWebServerResponse* response = req->beginChunkedResponse(
    "text/html",
    [html, offset](uint8_t* buffer, size_t maxLen, size_t /*index*/) -> size_t {
      size_t remaining = html->length() - *offset;
      if (remaining == 0) return 0;
      size_t toCopy = min(maxLen, remaining);
      memcpy(buffer, html->c_str() + *offset, toCopy);
      *offset += toCopy;
      return toCopy;
    }
  );

  req->send(response);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n===== VOICEGEST STARTING =====");
  bootTime = millis();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  buzz(80, 2);

  Wire.begin(21, 22);
  Wire.setClock(100000);
  delay(500);

  scanI2CBus();

  if (initMPU6050Raw()) {
    mpuAvailable = true;
  } else {
    Serial.println("[WARN] MPU6050 not found at 0x68 or 0x69 - tilt/shake/rotate disabled");
  }

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OK] OLED ready");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0); display.println("VOICEGEST");
    display.setCursor(0, 14); display.println(" Starting up...");
    display.display();
  } else {
    Serial.println("[WARN] OLED not found");
  }

  if (bmp.begin()) {
    bmpAvailable = true;
    Serial.println("[OK] BMP180 ready");
  } else {
    Serial.println("[WARN] BMP180 not found - breath detection disabled");
  }

  if (apds.begin()) {
    apdsAvailable = true;
    apds.enableColor(true);
    apds.enableProximity(true);
    apds.enableGesture(true);
    Serial.println("[OK] APDS9960 ready");
  } else {
    Serial.println("[WARN] APDS9960 not found - gesture/color disabled");
  }

  if (SPIFFS.begin(true)) {
    Serial.println("[OK] SPIFFS ready");
    rotateLogIfNeeded();
  } else {
    Serial.println("[ERROR] SPIFFS failed - check Partition Scheme = Default 4MB with spiffs");
  }

  logEvent("SYSTEM_START", "VoiceGest powered on");

  // Temporarily disabled - running BT + WiFi AP together roughly doubles
  // radio current draw, which is contributing to the brownout reset.
  // Re-enable once the dashboard is confirmed stable on its own.
  // SerialBT.begin("VoiceGest-BT");
  // Serial.println("[BT] Bluetooth Serial started as 'VoiceGest-BT'");

  // FIX: do not pass an empty password string to softAP(). Call it with
  // just the SSID for an open network - this was causing the boot loop.
  WiFi.softAP(WIFI_SSID);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // lower TX power to cut the current spike that's browning out the board
  Serial.println("[WIFI] Hotspot: " + String(WIFI_SSID));
  Serial.println("[WIFI] IP: " + WiFi.softAPIP().toString());

  server.on("/", HTTP_GET, handleDashboard);
  server.on("/last", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", lastSpokenPhrase);
  });
  server.on("/clearlog", HTTP_GET, [](AsyncWebServerRequest* req) {
    SPIFFS.remove(LOG_FILE);
    totalEvents = 0;
    req->send(200, "text/plain", "Log cleared");
  });
  server.begin();
  Serial.println("[OK] Dashboard live at http://192.168.4.1");

  readAllSensors();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("VOICEGEST READY!");
  display.setCursor(0, 12); display.println("Pair BT: VoiceGest-BT");
  display.setCursor(0, 24); display.println("WiFi: VoiceGest");
  display.setCursor(0, 36); display.println("Dash: 192.168.4.1");
  display.display();

  buzz(80, 1); delay(80); buzz(80, 1); delay(80); buzz(160, 1);
  Serial.println("===== SETUP COMPLETE =====\n");
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorPoll >= SENSOR_POLL_MS) {
    lastSensorPoll = now;
    readAllSensors();
    runGestureEngine();
  }

  if (now - lastOledUpdate >= OLED_REFRESH_MS) {
    lastOledUpdate = now;
    updateOLED();
  }

  // Without this, loop() spins with zero yield, starving the idle task.
  // That's fine until a real HTTP client connects - then AsyncTCP needs
  // CPU time to push the response, the idle task misses its window long
  // enough to trip the task watchdog, and the board panics/reboots mid
  // request. That's exactly what ERR_CONNECTION_ABORTED looks like.
  delay(1);
}
