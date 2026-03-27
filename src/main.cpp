
#include <NeoPixelBus.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <WiFiManager.h>
#include <time.h>
#include <sys/time.h>
#include <EEPROM.h>
#include <Wire.h>
#include <SPI.h>
#include <RTClib.h>

#if defined(ARDUINO_ARCH_ESP8266)
#define PIN D5                 // Pin connected to WS2812 data pin (D1 mini)
#elif defined(ARDUINO_ARCH_ESP32)
#define PIN 5                  // GPIO5 for ESP32
#else
#define PIN 5
#endif
#define NUM_SEG 4
#define NUM_DOT 1
#define NUM_LEDS (NUM_SEG * 7 + NUM_DOT * 2)

class RingCompat {
public:
  RingCompat(uint16_t pixelCount, uint8_t pin)
    : strip(pixelCount, pin), count(pixelCount), brightness(255) {}

  void begin() {
    strip.Begin();
    clear();
    show();
  }

  void show() {
    strip.Show();
  }

  void clear() {
    for (uint16_t i = 0; i < count; i++) {
      strip.SetPixelColor(i, RgbwColor(0, 0, 0, 0));
    }
  }

  void setBrightness(uint8_t value) {
    brightness = value;
  }

  uint8_t getBrightness() const {
    return brightness;
  }

  uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
  }

  void setPixelColor(int index, uint32_t packedColor) {
    if (index < 0 || index >= count) {
      return;
    }

    uint8_t r = (uint8_t)((packedColor >> 16) & 0xFF);
    uint8_t g = (uint8_t)((packedColor >> 8) & 0xFF);
    uint8_t b = (uint8_t)(packedColor & 0xFF);

    if (brightness < 255) {
      r = (uint8_t)((uint16_t)r * brightness / 255);
      g = (uint8_t)((uint16_t)g * brightness / 255);
      b = (uint8_t)((uint16_t)b * brightness / 255);
    }

    strip.SetPixelColor(index, RgbwColor(r, g, b, 0));
  }

  void fill(uint32_t packedColor) {
    for (uint16_t i = 0; i < count; i++) {
      setPixelColor(i, packedColor);
    }
  }

private:
  #if defined(ARDUINO_ARCH_ESP8266)
  NeoPixelBus<NeoGrbwFeature, NeoEsp8266BitBang800KbpsMethod> strip;
  #elif defined(ARDUINO_ARCH_ESP32)
  NeoPixelBus<NeoGrbwFeature, NeoEsp32Rmt0800KbpsMethod> strip;
  #else
  NeoPixelBus<NeoGrbwFeature, NeoEsp8266BitBang800KbpsMethod> strip;
  #endif
  uint16_t count;
  uint8_t brightness;
};

RingCompat ring(NUM_LEDS, PIN);

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer server(80);
#elif defined(ARDUINO_ARCH_ESP32)
WebServer server(80);
#endif
WiFiManager wm;
RTC_DS3231 rtc;
bool rtcAvailable = false;

char ntpServer[64] = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* NTP_SERVER_3 = "time.cloudflare.com";
char tzInfo[64] = "CET-1CEST,M3.5.0/2,M10.5.0/3"; // Europe/Rome (DST automatic)
const char* TZ_ROME = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const char* TZ_LONDON = "GMT0BST,M3.5.0/1,M10.5.0/2";
const char* TZ_UTC = "UTC0";
const char* TZ_NEWYORK = "EST5EDT,M3.2.0/2,M11.1.0/2";
const char* TZ_LOSANGELES = "PST8PDT,M3.2.0/2,M11.1.0/2";
const char* TZ_TOKYO = "JST-9";
const char* TZ_SYDNEY = "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
const char* TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* TZ_DUBAI = "GST-4";
const char* TZ_KOLKATA = "IST-5:30";
const char* TZ_SHANGHAI = "CST-8";
const char* TZ_MOSCOW = "MSK-3";
bool timeSynced = false;
bool wifiConnectedHandled = false;
unsigned long lastNtpRetryMs = 0;

struct SavedSettings {
  uint32_t magic;
  uint32_t colorQuadrants;
  uint32_t colorHourHand;
  uint32_t colorMinuteHand;
  uint32_t colorSecondHand;
  uint8_t showQuadrants;
  uint8_t quadrantMode;
  uint8_t hourHandMode;
  uint8_t timeSourceMode;
  uint8_t brightnessMode;
  uint8_t dimBrightness;
  uint8_t nightStartHour;
  uint8_t nightEndHour;
  char ntpServer[64];
  char tzInfo[64];
};

const uint32_t SETTINGS_MAGIC = 0xC10C2032;
const int EEPROM_SIZE = 512;

// Forward declarations
void rotatingRingAnimation();
void pulsatingGlowAnimation();
void progressBarAnimation();
void wifiSearchingAnimation();
void wifiConnectingAnimation();
void wifiConnectedAnimation();
void wifiFailedAnimation();
void displayClock();
void setDigit(uint8_t number, uint8_t index);
void handleRoot();
void handleUpdate();
void handleSetTime();
void handleTestAnimation();
void applyTimezone();
int wrapLedIndex(int index);
uint32_t hexToColor(String hex);
String colorToHex(uint32_t color);
uint32_t applyGammaCorrection(uint32_t color);
bool syncTimeWithNTP();
void loadSettings();
void saveSettings();
uint8_t getActiveBrightness(const tm* localNow = nullptr);
uint32_t scaleColorBrightness(uint32_t color, uint8_t brightness);
void logRtcTime(const char* prefix);

// Default settings
uint32_t colorQuadrants = ring.Color(255, 255, 255);   // legacy (unused)
uint32_t colorHourHand = ring.Color(255, 0, 0);        // legacy (unused)
uint32_t colorMinuteHand = ring.Color(255, 255, 255);  // legacy (unused)
uint32_t colorSecondHand = ring.Color(0, 0, 255);      // legacy (unused)
bool showQuadrants = false;                            // legacy (unused)
uint8_t quadrantMode = 12; // legacy (unused)
uint8_t hourHandMode = 0;  // legacy (unused)
// Brightness profiles: 0 = full always, 1 = auto night, 2 = dim always.
uint8_t brightnessMode = 0;
// Time source: 0 = NTP (with RTC fallback), 1 = DS3231 only.
uint8_t timeSourceMode = 0;
uint8_t dimBrightness = 80;
uint8_t nightStartHour = 22;
uint8_t nightEndHour = 7;
const uint8_t NORMAL_BRIGHTNESS = 255;

byte digits[12][7] = {
  {1, 1, 0, 1, 1, 1, 1},   // Digit 0
  {0, 0, 0, 1, 0, 0, 1},   // Digit 1
  {1, 1, 1, 1, 1, 0, 0},   // Digit 2
  {1, 0, 1, 1, 1, 0, 1},   // Digit 3
  {0, 0, 1, 1, 0, 1, 1},   // Digit 4
  {1, 0, 1, 0, 1, 1, 1},   // Digit 5
  {1, 1, 1, 0, 1, 1, 1},   // Digit 6
  {0, 0, 0, 1, 1, 0, 1},   // Digit 7
  {1, 1, 1, 1, 1, 1, 1},   // Digit 8
  {1, 0, 1, 1, 1, 1, 1},   // Digit 9
  {0, 0, 1, 1, 1, 1, 0},   // Digit o
  {1, 1, 0, 0, 1, 1, 0}    // Digit C
};

byte segmentState[NUM_SEG * 7] = {0};
uint16_t transitionStep = 0;

void setup() {
  Serial.begin(9600);

  Wire.begin();
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("DS3231 not found");
  } else {
    logRtcTime("DS3231 startup time");
  }

  applyTimezone();

  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  // Initialize NeoPixel Ring
  ring.begin();
  ring.setBrightness(NORMAL_BRIGHTNESS);
  ring.show();


  // Play startup animations
  rotatingRingAnimation();
  pulsatingGlowAnimation();
  progressBarAnimation();
  // Wi-Fi setup with captive portal (no hardcoded SSID/password)
  // Non-blocking mode keeps LED animations alive while portal is active.
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(120); // Timeout after 3 minutes if not configured

  Serial.println("Starting Wi-Fi auto connect...");
  bool connected = wm.autoConnect("Clock-Setup");
  if (connected) {
    Serial.println("Wi-Fi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    wifiConnectedAnimation();
    if (timeSourceMode == 0) {
      timeSynced = syncTimeWithNTP();
    } else {
      timeSynced = rtcAvailable && (rtc.now().unixtime() > 100000);
    }
    wifiConnectedHandled = true;
  } else {
    Serial.println("Starting Web Portal (non-blocking)");
  }

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/update", handleUpdate);
  server.on("/setTime", handleSetTime);
  server.on("/testAnimation", handleTestAnimation);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  wm.process();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedHandled) {
      Serial.println("Wi-Fi connected");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      wifiConnectedAnimation();
      if (timeSourceMode == 0) {
        timeSynced = syncTimeWithNTP();
      } else {
        timeSynced = rtcAvailable && (rtc.now().unixtime() > 100000);
      }
      wifiConnectedHandled = true;
    }

    if (timeSourceMode == 0 && !timeSynced && (millis() - lastNtpRetryMs > 30000UL)) {
      lastNtpRetryMs = millis();
      timeSynced = syncTimeWithNTP();
    }

    displayClock();
  } else {
    wifiSearchingAnimation();
  }

  server.handleClient();
}

void displayClock() {
  applyTimezone();

  time_t nowEpoch = time(nullptr);

  if (timeSourceMode == 1 && rtcAvailable) {
    DateTime rtcNow = rtc.now();
    nowEpoch = rtcNow.unixtime();
  }

  if (nowEpoch < 100000) {
    if (rtcAvailable) {
      DateTime rtcNow = rtc.now();
      nowEpoch = rtcNow.unixtime();
      logRtcTime("Using DS3231 fallback");
    }

    if (nowEpoch < 100000) {
      ring.clear();
      ring.show();
      delay(1000);
      return;
    }
  }

  struct tm now;
  localtime_r(&nowEpoch, &now);
  uint8_t activeBrightness = getActiveBrightness(&now);
  ring.setBrightness(activeBrightness);

  uint8_t hours = (uint8_t)now.tm_hour;
  uint8_t minutes = (uint8_t)now.tm_min;

  setDigit(hours, 1);
  setDigit(minutes, 2);

  transitionStep += 8;
  if (transitionStep > 254) {
    for (uint16_t i = 0; i < NUM_SEG * 7; i++) {
      if (segmentState[i] == 2) {
        segmentState[i] = 0;
      }
      if (segmentState[i] == 3) {
        segmentState[i] = 1;
      }
    }
    transitionStep = 0;
  }

  for (uint16_t i = 0; i < NUM_SEG; i++) {
    for (uint16_t j = 0; j < 7; j++) {
      uint16_t ledIndex = i * 7 + j;
      if (segmentState[ledIndex] == 0) {
        ring.setPixelColor(ledIndex, ring.Color(0, 0, 0));
      } else if (segmentState[ledIndex] == 1) {
        ring.setPixelColor(ledIndex, ring.Color(255, 255, 255));
      } else if (segmentState[ledIndex] == 2) {
        uint8_t fade = (uint8_t)(254 - transitionStep);
        ring.setPixelColor(ledIndex, ring.Color(fade, fade, fade));
      } else {
        ring.setPixelColor(ledIndex, ring.Color(transitionStep, transitionStep, transitionStep));
      }
    }
  }

  if ((now.tm_sec % 2) == 0) {
    ring.setPixelColor(NUM_SEG * 7, ring.Color(0, 0, 153));
    ring.setPixelColor(NUM_SEG * 7 + 1, ring.Color(0, 0, 153));
  } else {
    ring.setPixelColor(NUM_SEG * 7, ring.Color(153, 0, 0));
    ring.setPixelColor(NUM_SEG * 7 + 1, ring.Color(153, 0, 0));
  }

  ring.show();
  delay(10);
}

void setDigit(uint8_t number, uint8_t index) {
  if (index < 1 || index > (NUM_SEG / 2)) {
    return;
  }

  for (uint8_t i = 1; i <= 2; i++) {
    uint8_t digit = number % 10;
    number /= 10;
    for (uint8_t j = 0; j < 7; j++) {
      uint16_t segmentIndex = (uint16_t)((index * 2 - i) * 7 + j);
      if (segmentIndex >= (NUM_SEG * 7)) {
        continue;
      }
      if ((digits[digit][j] == 0) && (segmentState[segmentIndex] == 1)) {
        segmentState[segmentIndex] = 2;
        transitionStep = 0;
      }
      if ((digits[digit][j] == 1) && (segmentState[segmentIndex] == 0)) {
        segmentState[segmentIndex] = 3;
        transitionStep = 0;
      }
    }
  }
}

void handleRoot() {
  applyTimezone();
  time_t nowEpoch = time(nullptr);
  if (nowEpoch < 100000 && rtcAvailable) {
    DateTime rtcNow = rtc.now();
    nowEpoch = rtcNow.unixtime();
  }
  struct tm now;
  localtime_r(&nowEpoch, &now);

  char manualDateTimeValue[24] = "";
  if (nowEpoch > 100000) {
    strftime(manualDateTimeValue, sizeof(manualDateTimeValue), "%Y-%m-%dT%H:%M:%S", &now);
  }

  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px;}";
  html += ".card{max-width:720px;margin:0 auto;background:#111827;border:1px solid #334155;border-radius:12px;padding:18px;}";
  html += "h1{margin:0 0 8px 0;font-size:22px;}h2{margin:20px 0 10px 0;font-size:16px;color:#93c5fd;}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;}@media(max-width:680px){.grid{grid-template-columns:1fr;}}";
  html += "label{font-size:13px;color:#cbd5e1;display:block;margin-bottom:6px;}";
  html += "input[type='color'],input[type='number'],input[type='text']{width:100%;height:40px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;padding:0 10px;box-sizing:border-box;}";
  html += ".row{margin-bottom:12px;} .check{display:flex;align-items:center;gap:8px;margin:12px 0;}";
  html += "button{background:#2563eb;color:white;border:none;border-radius:8px;padding:10px 14px;font-weight:600;cursor:pointer;}";
  html += "small{color:#94a3b8;} .status{margin:8px 0 14px 0;padding:10px;border-radius:8px;background:#0b1220;border:1px solid #334155;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>WS2812 LED Ring Clock</h1>";
  html += "<div class='status'>";
  if (nowEpoch < 100000) {
    html += "Current Time: not synced (NTP)<br>";
  } else {
  html += "Current Time: " + String(now.tm_hour) + ":" + String(now.tm_min) + ":" + String(now.tm_sec) + "<br>";
  }
  html += "IP: " + WiFi.localIP().toString() + "<br>";
  html += "Time Source: ";
  html += (timeSourceMode == 1) ? "DS3231" : "NTP (with DS3231 fallback)";
  html += "<br>";
  html += "NTP: " + String(ntpServer) + "<br>";
  html += "TZ: " + String(tzInfo);
  html += "</div>";

  html += "<h2>Animation Test</h2>";
  html += "<form action='/testAnimation' method='POST'>";
  html += "<div class='row'><label>Select animation</label>";
  html += "<select name='animation' style='width:100%;height:40px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;padding:0 10px;box-sizing:border-box;'>";
  html += "<option value='rotating'>Rotating Ring</option>";
  html += "<option value='pulsating'>Pulsating Glow</option>";
  html += "<option value='progress'>Progress Bar</option>";
  html += "<option value='wifiSearching'>WiFi Searching</option>";
  html += "<option value='wifiConnecting'>WiFi Connecting</option>";
  html += "<option value='wifiConnected'>WiFi Connected</option>";
  html += "<option value='wifiFailed'>WiFi Failed</option>";
  html += "</select></div>";
  html += "<button type='submit'>Run Animation</button>";
  html += "</form>";

  html += "<h2>Time Sync</h2>";
  html += "<div class='row'><label>Set Time Manually</label>";
  html += "<form action='/setTime' method='POST'>";
  html += "<input type='datetime-local' step='1' name='manualDateTime' value='" + String(manualDateTimeValue) + "'>";
  html += "<small>Uses selected timezone from this page.</small><br><br>";
  html += "<button type='submit'>Set Time</button>";
  html += "</form></div>";

  html += "<form action='/update' method='POST'>";
  html += "<h2>LED Colors</h2><div class='grid'>";
  html += "<div class='row'><label>Segment Color (white)</label><input type='color' name='segmentColor' value='#FFFFFF'></div>";
  html += "</div>";
  html += "<h2>Display</h2>";
  html += "<small>La visualizzazione a 7 segmenti non usa quadranti o lancette.</small><br><br>";
  html += "<h2>Brightness</h2>";
  html += "<div class='row'><label>Brightness Mode</label>";
  html += "<select name='brightnessMode' style='width:100%;height:40px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;padding:0 10px;box-sizing:border-box;'>";
  html += "<option value='0'";
  if (brightnessMode == 0) {
    html += " selected";
  }
  html += ">Always full (255)</option>";
  html += "<option value='1'";
  if (brightnessMode == 1) {
    html += " selected";
  }
  html += ">Night mode (automatic by hour)</option>";
  html += "<option value='2'";
  if (brightnessMode == 2) {
    html += " selected";
  }
  html += ">Always dim (permanent)</option>";
  html += "</select></div>";
  html += "<div class='grid'>";
  html += "<div class='row'><label>Dim Brightness (1-255)</label><input type='number' name='dimBrightness' min='1' max='255' value='" + String(dimBrightness) + "'></div>";
  html += "<div class='row'><label>Night Start Hour (0-23)</label><input type='number' name='nightStartHour' min='0' max='23' value='" + String(nightStartHour) + "'></div>";
  html += "<div class='row'><label>Night End Hour (0-23)</label><input type='number' name='nightEndHour' min='0' max='23' value='" + String(nightEndHour) + "'></div>";
  html += "</div>";
  html += "<small>Night mode dims from start hour to end hour (example: 22 to 7).</small><br><br>";
  html += "<h2>Time Sync</h2>";
  html += "<div class='row'><label>Time Source</label>";
  html += "<select name='timeSourceMode' style='width:100%;height:40px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;padding:0 10px;box-sizing:border-box;'>";
  html += "<option value='0'";
  if (timeSourceMode == 0) {
    html += " selected";
  }
  html += ">NTP server (fallback DS3231)</option>";
  html += "<option value='1'";
  if (timeSourceMode == 1) {
    html += " selected";
  }
  html += ">DS3231 only</option>";
  html += "</select></div>";
  html += "<div class='row'><label>NTP Server</label><input type='text' name='ntpServer' maxlength='63' value='" + String(ntpServer) + "'></div>";
  html += "<div class='row'><label>Timezone</label>";
  html += "<select name='tzPreset' style='width:100%;height:40px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;padding:0 10px;box-sizing:border-box;'>";
  html += "<option value='rome'";
  if (strcmp(tzInfo, TZ_ROME) == 0) {
    html += " selected";
  }
  html += ">Europe/Rome</option>";
  html += "<option value='london'";
  if (strcmp(tzInfo, TZ_LONDON) == 0) {
    html += " selected";
  }
  html += ">Europe/London</option>";
  html += "<option value='utc'";
  if (strcmp(tzInfo, TZ_UTC) == 0) {
    html += " selected";
  }
  html += ">UTC</option>";
  html += "<option value='newyork'";
  if (strcmp(tzInfo, TZ_NEWYORK) == 0) {
    html += " selected";
  }
  html += ">America/New_York</option>";
  html += "<option value='losangeles'";
  if (strcmp(tzInfo, TZ_LOSANGELES) == 0) {
    html += " selected";
  }
  html += ">America/Los_Angeles</option>";
  html += "<option value='tokyo'";
  if (strcmp(tzInfo, TZ_TOKYO) == 0) {
    html += " selected";
  }
  html += ">Asia/Tokyo</option>";
  html += "<option value='sydney'";
  if (strcmp(tzInfo, TZ_SYDNEY) == 0) {
    html += " selected";
  }
  html += ">Australia/Sydney</option>";
  html += "<option value='berlin'";
  if (strcmp(tzInfo, TZ_BERLIN) == 0) {
    html += " selected";
  }
  html += ">Europe/Berlin</option>";
  html += "<option value='dubai'";
  if (strcmp(tzInfo, TZ_DUBAI) == 0) {
    html += " selected";
  }
  html += ">Asia/Dubai</option>";
  html += "<option value='kolkata'";
  if (strcmp(tzInfo, TZ_KOLKATA) == 0) {
    html += " selected";
  }
  html += ">Asia/Kolkata</option>";
  html += "<option value='shanghai'";
  if (strcmp(tzInfo, TZ_SHANGHAI) == 0) {
    html += " selected";
  }
  html += ">Asia/Shanghai</option>";
  html += "<option value='moscow'";
  if (strcmp(tzInfo, TZ_MOSCOW) == 0) {
    html += " selected";
  }
  html += ">Europe/Moscow</option>";
  html += "</select></div>";
  html += "<small>Esempio: pool.ntp.org, time.google.com</small><br><br>";
  html += "<button type='submit'>Save Settings</button>";
  html += "</form>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleUpdate() {
  bool ntpServerChanged = false;
  bool timezoneChanged = false;

  if (server.hasArg("segmentColor")) {
    (void)server.arg("segmentColor");
  }
  if (server.hasArg("brightnessMode")) {
    int mode = server.arg("brightnessMode").toInt();
    if (mode < 0) {
      mode = 0;
    }
    if (mode > 2) {
      mode = 2;
    }
    brightnessMode = (uint8_t)mode;
  }
  if (server.hasArg("timeSourceMode")) {
    int mode = server.arg("timeSourceMode").toInt();
    timeSourceMode = (mode == 1) ? 1 : 0;
    if (timeSourceMode == 1) {
      timeSynced = rtcAvailable && (rtc.now().unixtime() > 100000);
    }
  }
  if (server.hasArg("dimBrightness")) {
    int b = server.arg("dimBrightness").toInt();
    if (b < 1) {
      b = 1;
    }
    if (b > 255) {
      b = 255;
    }
    dimBrightness = (uint8_t)b;
  }
  if (server.hasArg("nightStartHour")) {
    int h = server.arg("nightStartHour").toInt();
    if (h < 0) {
      h = 0;
    }
    if (h > 23) {
      h = 23;
    }
    nightStartHour = (uint8_t)h;
  }
  if (server.hasArg("nightEndHour")) {
    int h = server.arg("nightEndHour").toInt();
    if (h < 0) {
      h = 0;
    }
    if (h > 23) {
      h = 23;
    }
    nightEndHour = (uint8_t)h;
  }
  if (server.hasArg("ntpServer")) {
    String inputNtp = server.arg("ntpServer");
    inputNtp.trim();
    if (inputNtp.length() > 0 && inputNtp.length() < (int)sizeof(ntpServer)) {
      if (strncmp(ntpServer, inputNtp.c_str(), sizeof(ntpServer) - 1) != 0) {
        inputNtp.toCharArray(ntpServer, sizeof(ntpServer));
        ntpServerChanged = true;
      }
    }
  }
  if (server.hasArg("tzPreset")) {
    String tzPreset = server.arg("tzPreset");
    const char* newTz = TZ_ROME;
    if (tzPreset == "london") {
      newTz = TZ_LONDON;
    } else if (tzPreset == "utc") {
      newTz = TZ_UTC;
    } else if (tzPreset == "newyork") {
      newTz = TZ_NEWYORK;
    } else if (tzPreset == "losangeles") {
      newTz = TZ_LOSANGELES;
    } else if (tzPreset == "tokyo") {
      newTz = TZ_TOKYO;
    } else if (tzPreset == "sydney") {
      newTz = TZ_SYDNEY;
    } else if (tzPreset == "berlin") {
      newTz = TZ_BERLIN;
    } else if (tzPreset == "dubai") {
      newTz = TZ_DUBAI;
    } else if (tzPreset == "kolkata") {
      newTz = TZ_KOLKATA;
    } else if (tzPreset == "shanghai") {
      newTz = TZ_SHANGHAI;
    } else if (tzPreset == "moscow") {
      newTz = TZ_MOSCOW;
    }

    if (strncmp(tzInfo, newTz, sizeof(tzInfo) - 1) != 0) {
      strncpy(tzInfo, newTz, sizeof(tzInfo));
      tzInfo[sizeof(tzInfo) - 1] = '\0';
      timezoneChanged = true;
    }
  }

  if (timezoneChanged) {
    applyTimezone();
  }
  if (ntpServerChanged) {
    timeSynced = syncTimeWithNTP();
  }

  saveSettings();

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Updated");
}

void handleSetTime() {
  if (!server.hasArg("manualDateTime")) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Missing manualDateTime");
    return;
  }

  String manualDateTime = server.arg("manualDateTime");
  manualDateTime.trim();

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  int parsed = sscanf(manualDateTime.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  if (parsed < 5) {
    server.send(400, "text/plain", "Invalid datetime format");
    return;
  }
  if (parsed == 5) {
    second = 0;
  }

  tm localTm = {};
  localTm.tm_year = year - 1900;
  localTm.tm_mon = month - 1;
  localTm.tm_mday = day;
  localTm.tm_hour = hour;
  localTm.tm_min = minute;
  localTm.tm_sec = second;
  localTm.tm_isdst = -1;

  applyTimezone();
  time_t epoch = mktime(&localTm);
  if (epoch < 100000) {
    server.send(400, "text/plain", "Datetime out of range");
    return;
  }

  timeval tv = {};
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  timeSynced = true;

  if (rtcAvailable) {
    rtc.adjust(DateTime((uint32_t)epoch));
    logRtcTime("DS3231 set from Web UI");
  }

  struct tm confirmTm;
  localtime_r(&epoch, &confirmTm);
  char confirmBuf[32];
  strftime(confirmBuf, sizeof(confirmBuf), "%Y-%m-%d %H:%M:%S", &confirmTm);
  Serial.println(String("Manual time set from Web UI: ") + confirmBuf + " TZ=" + tzInfo);

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Time updated");
}

void handleTestAnimation() {
  if (!server.hasArg("animation")) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "No animation selected");
    return;
  }

  String animation = server.arg("animation");

  if (animation == "rotating") {
    rotatingRingAnimation();
  } else if (animation == "pulsating") {
    pulsatingGlowAnimation();
  } else if (animation == "progress") {
    progressBarAnimation();
  } else if (animation == "wifiSearching") {
    for (int i = 0; i < NUM_LEDS; i++) {
      wifiSearchingAnimation();
    }
  } else if (animation == "wifiConnecting") {
    for (int i = 0; i < 20; i++) {
      wifiConnectingAnimation();
    }
  } else if (animation == "wifiConnected") {
    wifiConnectedAnimation();
  } else if (animation == "wifiFailed") {
    wifiFailedAnimation();
  }

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Animation executed");
}

uint32_t hexToColor(String hex) {
  long number = strtol(&hex[1], NULL, 16);
  return ring.Color((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
}

String colorToHex(uint32_t color) {
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", (uint8_t)((color >> 16) & 0xFF), (uint8_t)((color >> 8) & 0xFF), (uint8_t)(color & 0xFF));
  return String(hex);
}

uint32_t applyGammaCorrection(uint32_t color) {
  return color;
}

uint32_t scaleColorBrightness(uint32_t color, uint8_t brightness) {
  if (brightness >= 255) {
    return color;
  }

  uint8_t r = (uint8_t)((color >> 16) & 0xFF);
  uint8_t g = (uint8_t)((color >> 8) & 0xFF);
  uint8_t b = (uint8_t)(color & 0xFF);

  r = (uint8_t)((uint16_t)r * brightness / 255);
  g = (uint8_t)((uint16_t)g * brightness / 255);
  b = (uint8_t)((uint16_t)b * brightness / 255);

  return ring.Color(r, g, b);
}

int wrapLedIndex(int index) {
  if (NUM_LEDS <= 0) {
    return 0;
  }
  index %= NUM_LEDS;
  if (index < 0) {
    index += NUM_LEDS;
  }
  return index;
}

bool syncTimeWithNTP() {
  if (timeSourceMode == 1) {
    return rtcAvailable && (rtc.now().unixtime() > 100000);
  }

  Serial.println("Syncing time with NTP...");
  #if defined(ARDUINO_ARCH_ESP8266)
  // ESP8266: overload with TZ string.
  configTime(tzInfo, ntpServer, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ARDUINO_ARCH_ESP32)
  // ESP32: use configTzTime for POSIX TZ support.
  configTzTime(tzInfo, ntpServer, NTP_SERVER_2, NTP_SERVER_3);
  #endif

  for (int i = 0; i < 60; i++) {
    time_t now = time(nullptr);
    if (now > 100000) {
      struct tm localNow;
      localtime_r(&now, &localNow);
      char timeBuf[32];
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localNow);
      if (rtcAvailable) {
        rtc.adjust(DateTime(now));
        logRtcTime("DS3231 adjusted from NTP");
      }
      Serial.println("NTP sync successful");
      Serial.println(String("Local time after sync: ") + timeBuf + " TZ=" + tzInfo);
      return true;
    }
    delay(250);
  }

  Serial.println("NTP sync failed");
  return false;
}

void logRtcTime(const char* prefix) {
  if (!rtcAvailable) {
    return;
  }

  DateTime rtcNow = rtc.now();
  char rtcBuf[32];
  snprintf(
    rtcBuf,
    sizeof(rtcBuf),
    "%04d-%02d-%02d %02d:%02d:%02d",
    rtcNow.year(),
    rtcNow.month(),
    rtcNow.day(),
    rtcNow.hour(),
    rtcNow.minute(),
    rtcNow.second()
  );

  Serial.println(String(prefix) + ": " + rtcBuf + " (epoch=" + String((unsigned long)rtcNow.unixtime()) + ")");
}

void applyTimezone() {
  setenv("TZ", tzInfo, 1);
  tzset();
}

uint8_t getActiveBrightness(const tm* localNow) {
  if (brightnessMode == 2) {
    return dimBrightness;
  }

  if (brightnessMode == 1) {
    tm nowLocal;
    const tm* nowPtr = localNow;
    if (nowPtr == nullptr) {
      time_t nowEpoch = time(nullptr);
      localtime_r(&nowEpoch, &nowLocal);
      nowPtr = &nowLocal;
    }

    int hour = nowPtr->tm_hour;
    bool isNight;
    if (nightStartHour == nightEndHour) {
      isNight = true;
    } else if (nightStartHour < nightEndHour) {
      isNight = (hour >= nightStartHour && hour < nightEndHour);
    } else {
      isNight = (hour >= nightStartHour || hour < nightEndHour);
    }

    if (isNight) {
      return dimBrightness;
    }
  }

  return NORMAL_BRIGHTNESS;
}

void loadSettings() {
  SavedSettings s;
  EEPROM.get(0, s);

  if (s.magic != SETTINGS_MAGIC) {
    Serial.println("No saved settings found, using defaults");
    return;
  }

  colorQuadrants = s.colorQuadrants;
  colorHourHand = s.colorHourHand;
  colorMinuteHand = s.colorMinuteHand;
  colorSecondHand = s.colorSecondHand;
  showQuadrants = (s.showQuadrants != 0);
  quadrantMode = (s.quadrantMode == 4) ? 4 : 12;
  hourHandMode = (s.hourHandMode == 1) ? 1 : 0;
  timeSourceMode = (s.timeSourceMode <= 1) ? s.timeSourceMode : 0;
  brightnessMode = (s.brightnessMode <= 2) ? s.brightnessMode : 0;
  dimBrightness = (s.dimBrightness >= 1) ? s.dimBrightness : 80;
  nightStartHour = (s.nightStartHour <= 23) ? s.nightStartHour : 22;
  nightEndHour = (s.nightEndHour <= 23) ? s.nightEndHour : 7;
  if (strlen(s.ntpServer) > 0 && strlen(s.ntpServer) < sizeof(ntpServer)) {
    strncpy(ntpServer, s.ntpServer, sizeof(ntpServer));
    ntpServer[sizeof(ntpServer) - 1] = '\0';
  }
  if (strlen(s.tzInfo) > 0 && strlen(s.tzInfo) < sizeof(tzInfo)) {
    strncpy(tzInfo, s.tzInfo, sizeof(tzInfo));
    tzInfo[sizeof(tzInfo) - 1] = '\0';
  }

  Serial.println(String("Loaded NTP from EEPROM: ") + ntpServer);
  Serial.println(String("Loaded TZ from EEPROM: ") + tzInfo);
  Serial.println("Settings loaded from EEPROM");
}

void saveSettings() {
  SavedSettings s;
  s.magic = SETTINGS_MAGIC;
  s.colorQuadrants = colorQuadrants;
  s.colorHourHand = colorHourHand;
  s.colorMinuteHand = colorMinuteHand;
  s.colorSecondHand = colorSecondHand;
  s.showQuadrants = showQuadrants ? 1 : 0;
  s.quadrantMode = (quadrantMode == 4) ? 4 : 12;
  s.hourHandMode = (hourHandMode == 1) ? 1 : 0;
  s.timeSourceMode = (timeSourceMode == 1) ? 1 : 0;
  s.brightnessMode = (brightnessMode <= 2) ? brightnessMode : 0;
  s.dimBrightness = (dimBrightness >= 1) ? dimBrightness : 80;
  s.nightStartHour = (nightStartHour <= 23) ? nightStartHour : 22;
  s.nightEndHour = (nightEndHour <= 23) ? nightEndHour : 7;
  strncpy(s.ntpServer, ntpServer, sizeof(s.ntpServer));
  s.ntpServer[sizeof(s.ntpServer) - 1] = '\0';
  strncpy(s.tzInfo, tzInfo, sizeof(s.tzInfo));
  s.tzInfo[sizeof(s.tzInfo) - 1] = '\0';

  EEPROM.put(0, s);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}






void rotatingRingAnimation() {
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.clear();
    ring.setPixelColor(i, ring.Color(0, 0, 255)); // Blue
    ring.show();
    delay(50); // Adjust speed as needed
  }
}

void pulsatingGlowAnimation() {
  for (int brightness = 0; brightness <= 255; brightness += 5) {
    ring.setBrightness(brightness);
    ring.fill(ring.Color(0, 0, 255)); // Blue
    ring.show();
    delay(20);
  }
  for (int brightness = 255; brightness >= 0; brightness -= 5) {
    ring.setBrightness(brightness);
    ring.fill(ring.Color(0, 0, 255)); // Blue
    ring.show();
    delay(20);
  }
  ring.setBrightness(NORMAL_BRIGHTNESS);
}


void progressBarAnimation() {
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(0, 255, 0)); // Green
    ring.show();
    delay(50); // Adjust progress speed
  }
}


void wifiSearchingAnimation() {
  static int pos = 0;
  ring.clear();
  ring.setPixelColor(pos, ring.Color(0, 0, 255)); // Blue
  pos = (pos + 1) % NUM_LEDS;
  ring.show();
  delay(100); // Adjust speed as needed
}


void wifiConnectingAnimation() {
  for (int i = 0; i < NUM_LEDS; i++) {
    int brightness = (sin(i * 0.2) + 1) * 127; // Sine wave effect
    ring.setPixelColor(i, ring.Color(0, brightness, brightness)); // Cyan
  }
  ring.show();
  delay(100); // Adjust wave speed
}



void wifiConnectedAnimation() {
  for (int i = 0; i < 3; i++) { // Flash three times
    ring.fill(ring.Color(0, 255, 0)); // Green
    ring.show();
    delay(200);
    ring.clear();
    ring.show();
    delay(200);
  }
}


void wifiFailedAnimation() {
  for (int i = 0; i < 3; i++) { // Flash three times
    ring.fill(ring.Color(255, 0, 0)); // Red
    ring.show();
    delay(200);
    ring.clear();
    ring.show();
    delay(200);
  }
}



