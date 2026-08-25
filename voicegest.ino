// ============================================================
// VOICEGEST — Gesture-to-Speech AAC Device
// IEEE International MYOSA Event 6.0 | IEEE BioSensors 2026
// Team: Cipher | GEC Thrissur, Kerala
// Hardware: MYOSA Kit ONLY (Motherboard + MPU6050 + APDS9960 +
//           BMP180 + OLED SSD1306 + onboard buzzer)
// ============================================================
//
// LIBRARIES REQUIRED (install via Library Manager):
// - Adafruit MPU6050
// - Adafruit Unified Sensor
// - Adafruit BMP085 Unified
// - Adafruit APDS9960 Library
// - Adafruit SSD1306
// - Adafruit GFX Library
// - ArduinoJson (v7.x)
// - ESPAsyncWebServer
// - AsyncTCP
//
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <BluetoothSerial.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_APDS9960.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <vector>

// ============================================================
// SECTION 1: SETTINGS
// ============================================================

const char* WIFI_SSID = "VoiceGest";
const char* WIFI_PASSWORD = "";

#define BUZZER_PIN 12
#define OLED_RESET_PIN -1

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// --- MPU6050 address is now auto-detected in setup(), see
//     detectMPUAddress(). These two are the only valid options
//     for this chip: 0x68 = AD0 pin LOW/floating (most common),
//     0x69 = AD0 pin pulled HIGH.
#define MPU_ADDR_LOW  0x68
#define MPU_ADDR_HIGH 0x69
#define OLED_ADDR 0x3C

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

// ============================================================
// SECTION 2: OBJECTS
// ============================================================

Adafruit_MPU6050 mpu;
Adafruit_BMP085_Unified bmp(10085);
Adafruit_APDS9960 apds;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
BluetoothSerial SerialBT;
AsyncWebServer server(80);

bool mpuAvailable = false;
bool bmpAvailable = false;
bool apdsAvailable = false;

// ============================================================
// SECTION 3: GLOBAL STATE
// ============================================================

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

// ============================================================
// SECTION 4: HELPERS
// ============================================================

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

// ============================================================
// SECTION 4b: I2C DIAGNOSTICS
// ============================================================

// Scans the whole I2C bus and prints every address that responds.
// Run this any time a sensor "goes missing" — it tells you
// definitively what the ESP32 can actually see on the wire,
// independent of any library's begin() logic.
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

// The MPU6050 replies at 0x68 when its AD0 pin is LOW/floating
// (the common default on breakout boards) or 0x69 when AD0 is
// pulled HIGH. Rather than hardcoding one address and failing
// silently if the board wires AD0 the other way, we just try
// both and use whichever one answers.
bool detectAndBeginMPU() {
  if (mpu.begin(MPU_ADDR_LOW, &Wire)) {
    Serial.println("[OK] MPU6050 ready at 0x68");
    return true;
  }
  if (mpu.begin(MPU_ADDR_HIGH, &Wire)) {
    Serial.println("[OK] MPU6050 ready at 0x69");
    return true;
  }
  return false;
}

// ============================================================
// SECTION 5: SENSOR READ
// ============================================================

void readAllSensors() {
  if (mpuAvailable) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    accelX = a.acceleration.x;
    accelY = a.acceleration.y;
    accelZ = a.acceleration.z;
    gyroX = g.gyro.x * 57.2958;
    gyroY = g.gyro.y * 57.2958;
    gyroZ = g.gyro.z * 57.2958;
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

// ============================================================
// SECTION 6: GESTURE CLASSIFICATION
// ============================================================

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

// ============================================================
// SECTION 7: MAIN GESTURE ENGINE
// ============================================================

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

// ============================================================
// SECTION 8: OLED DISPLAY
// ============================================================

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

// ============================================================
// SECTION 9: WEB DASHBOARD
// ============================================================

String buildDashboard() {
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
    int start = max(0, (int)lines.size() - 30);
    for (int i = (int)lines.size() - 1; i >= start; i--) {
      StaticJsonDocument<256> doc;
      if (!deserializeJson(doc, lines[i])) {
        rows += "<tr><td>" + doc["t"].as<String>() + "</td>";
        rows += "<td>" + doc["e"].as<String>() + "</td>";
        rows += "<td>" + doc["d"].as<String>() + "</td></tr>\n";
      }
    }
  }
  if (rows == "") rows = "<tr><td colspan='3' style='text-align:center;color:#666;padding:16px;'>No events yet</td></tr>";

  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='5'>";
  h += "<title>VoiceGest Dashboard</title><style>";
  h += "*{box-sizing:border-box;margin:0;padding:0;}";
  h += "body{font-family:-apple-system,Arial,sans-serif;background:#0d0d1a;color:#e0e0e0;padding:14px;max-width:520px;margin:auto;}";
  h += ".hdr{text-align:center;padding:18px 0;}";
  h += ".hdr h1{font-size:18px;color:#fff;}";
  h += ".hdr p{font-size:12px;color:#666;margin-top:4px;}";
  h += "table{width:100%;border-collapse:collapse;font-size:12px;margin-top:14px;}";
  h += "th{background:#161625;color:#666;padding:8px 6px;text-align:left;border-bottom:1px solid #2a2a45;}";
  h += "td{padding:8px 6px;border-bottom:1px solid #1e1e35;}";
  h += ".ftr{text-align:center;font-size:11px;color:#444;margin-top:16px;}";
  h += "</style></head><body>";
  h += "<div class='hdr'><h1>VOICEGEST — Caregiver Dashboard</h1>";
  h += "<p>IEEE MYOSA 6.0 &nbsp;|&nbsp; Team Cipher &nbsp;|&nbsp; GEC Thrissur</p></div>";
  h += "<table><tr><th>Time</th><th>Event</th><th>Detail</th></tr>" + rows + "</table>";
  h += "<div class='ftr'>Uptime: " + getUptime() + " | Total events: " + String(totalEvents) + "</div>";
  h += "</body></html>";
  return h;
}

// ============================================================
// SECTION 10: SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n===== VOICEGEST STARTING =====");
  bootTime = millis();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  buzz(80, 2);

  Wire.begin(21,22);
  delay(500);  // Give sensors time to power up

  // Print exactly what's alive on the bus before any library
  // begin() calls run - this is the fastest way to tell a wiring
  // problem apart from an address problem.
  scanI2CBus();

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

  if (detectAndBeginMPU()) {
    mpuAvailable = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("[WARN] MPU6050 not found at 0x68 or 0x69 - tilt/shake/rotate disabled");
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
  } else {
    Serial.println("[ERROR] SPIFFS failed - check Partition Scheme = Default 4MB with spiffs");
  }

  logEvent("SYSTEM_START", "VoiceGest powered on");

  SerialBT.begin("VoiceGest-BT");
  Serial.println("[BT] Bluetooth Serial started as 'VoiceGest-BT'");

  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WIFI] Hotspot: " + String(WIFI_SSID));
  Serial.println("[WIFI] IP: " + WiFi.softAPIP().toString());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", buildDashboard());
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

// ============================================================
// SECTION 11: LOOP
// ============================================================

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
}
