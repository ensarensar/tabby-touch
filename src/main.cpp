#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Bitbang.h>
#include <qrcode.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "pins.h"
#include "wifi_config.h"
#include "api_config.h"
#include "tr_ascii.h"

SET_LOOP_TASK_STACK_SIZE(28672);

TFT_eSPI tft;
XPT2046_Bitbang ts(TOUCH_MOSI, TOUCH_MISO, TOUCH_CLK, TOUCH_CS);

enum class AppState : uint8_t {
  Idle,
  KeypadRegno,
  Done,
};

static const uint32_t KEYPAD_TIMEOUT_MS = 30000;
static const uint32_t ACTION_TIMEOUT_MS = 20000;
static const uint32_t DONE_OK_MS = 10000;
static const uint32_t DONE_ERR_MS = 4500;
static const uint32_t PIN_TTL_DEFAULT_MS = 180000;
static const uint32_t TOUCH_COOLDOWN_MS = 700;
static const int TOUCH_Z_MIN = 200;
static const uint8_t REGNO_LEN = 6;
static const uint8_t PIN_LEN = 4;
static const uint8_t INPUT_MAX = 6;
static const uint16_t KEY_BLUE = 0x1B5F;
static const uint16_t KEY_ORANGE = 0xFC20;

AppState state = AppState::Idle;
String currentRegno;
String currentPin;
String currentName;
String lastTimeStr;
String lastMessage;
String inputPromptStr = "Sicil no (6 hane)";
bool lastOk = false;
uint32_t stateSinceMs = 0;
uint32_t pinDeadlineMs = 0;
uint32_t doneShowMs = DONE_OK_MS;

char pinBuf[INPUT_MAX + 1] = {0};
uint8_t pinLen = 0;
uint8_t inputMaxLen = REGNO_LEN;
bool inputMask = false;

bool touchDown = false;
uint32_t lastTouchMs = 0;
uint32_t ignoreTouchUntilMs = 0;
uint32_t lastKeyMs = 0;
uint32_t relayUntilMs = 0;
uint32_t remoteBannerUntilMs = 0;
bool buttonDown = false;
bool buttonFiredThisHold = false;
uint32_t buttonChangedMs = 0;
uint32_t buttonIgnoreUntilMs = 0;
volatile bool pendingRemoteOpen = false;
SemaphoreHandle_t httpMutex = nullptr;
bool komutEndpointVar = true;
uint32_t lastKomut404LogMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastQrMs = 0;
char qrKod[32] = {0};
volatile bool qrDirty = false;
char pendingOtaUrl[256] = {0};
volatile bool pendingOta = false;

struct Btn {
  int x, y, w, h;
};

const Btn BTN_PLUS = {12, 108, 142, 116};
const Btn BTN_MINUS = {166, 108, 142, 116};
static const uint16_t COL_PLUS = 0x05E0;
static const uint16_t COL_MINUS = TFT_RED;

enum class KeyKind : uint8_t { Digit, Del, Ok };

struct Key {
  Btn b;
  KeyKind kind;
  char digit;
  const char *label;
};

const int KEY_PAD = 8;
const int KEY_GAP = 6;
const int KEY_W = 97;
const int KEY_H = 39;
const int KEY_Y0 = 58;

Key keys[12];

struct ApiResult {
  int httpCode = 0;
  bool ok = false;
  bool success = false;
  bool found = false;
  bool verified = false;
  bool pinBekleniyor = false;
  bool kayit = false;
  bool yetkili = false;
  int oturumId = 0;
  int ttlSaniye = 0;
  String regno;
  String isim;
  String soyisim;
  String adSoyad;
  String message;
  String yon;
};

void enterState(AppState s);
void submitSicil();
void komutTask(void *arg);
bool lockHttp(uint32_t ms);
void unlockHttp();
void applyRemoteOpen();
void serviceButton();
void openDoor(const char *title);
void showStatus(const char *title, const char *detail);
void drawIdleQr();
void maybeRefreshQr();
void noteOtaFromDoc(const JsonDocument &doc);
void runPendingOta();

void relayWrite(bool on) {
  const bool level = RELAY_ACTIVE_HIGH ? on : !on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
  digitalWrite(TRIGGER_PIN, level ? HIGH : LOW);
}

void relayOff() {
  relayWrite(false);
  relayUntilMs = 0;
}

void relayPulse() {
  relayWrite(true);
  relayUntilMs = millis() + RELAY_ON_MS;
  Serial.printf("Tetik IO%d + role IO%d ON %u ms\n",
                TRIGGER_PIN, RELAY_PIN, (unsigned)RELAY_ON_MS);
}

void openDoor(const char *title) {
  relayPulse();
  if (state == AppState::Idle) {
    showStatus(title, "Kapi acildi");
    remoteBannerUntilMs = millis() + 2500;
  }
}

void serviceRelay() {
  if (relayUntilMs != 0 && (int32_t)(millis() - relayUntilMs) >= 0) {
    relayOff();
    Serial.println("Role OFF");
  }
}

void initKeys() {
  const char *digits[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
  for (int i = 0; i < 9; i++) {
    int col = i % 3;
    int row = i / 3;
    keys[i].b = {
        KEY_PAD + col * (KEY_W + KEY_GAP),
        KEY_Y0 + row * (KEY_H + KEY_GAP),
        KEY_W, KEY_H};
    keys[i].kind = KeyKind::Digit;
    keys[i].digit = digits[i][0];
    keys[i].label = digits[i];
  }

  int y3 = KEY_Y0 + 3 * (KEY_H + KEY_GAP);
  keys[9].b = {KEY_PAD, y3, KEY_W, KEY_H};
  keys[9].kind = KeyKind::Del;
  keys[9].digit = 0;
  keys[9].label = "SIL";

  keys[10].b = {KEY_PAD + (KEY_W + KEY_GAP), y3, KEY_W, KEY_H};
  keys[10].kind = KeyKind::Digit;
  keys[10].digit = '0';
  keys[10].label = "0";

  keys[11].b = {KEY_PAD + 2 * (KEY_W + KEY_GAP), y3, KEY_W, KEY_H};
  keys[11].kind = KeyKind::Ok;
  keys[11].digit = 0;
  keys[11].label = "OK";
}

bool hitTest(const Btn &b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

String nowTimeStr() {
  time_t now = time(nullptr);
  struct tm t;
  if (now > 1700000000 && localtime_r(&now, &t)) {
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return String(buf);
  }
  return "--:--:--";
}

int wifiBarCount(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -90) return 1;
  return 0;
}

uint16_t wifiBarColor(int bars) {
  if (bars >= 3) return TFT_GREEN;
  if (bars == 2) return TFT_YELLOW;
  if (bars == 1) return TFT_ORANGE;
  return TFT_DARKGREY;
}

static const uint16_t COL_HDR = 0x0863;
static const uint16_t COL_ACCENT = 0x07FF;
static const uint16_t COL_CARD = 0x10A4;
static const uint16_t COL_MUTED = 0x7BEF;

void drawSignalBars(int x, int y, int rssi, bool connected) {
  const int barW = 5;
  const int gap = 2;
  const int maxH = 16;
  int bars = connected ? wifiBarCount(rssi) : 0;
  uint16_t onColor = wifiBarColor(bars);
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 4;
    int bx = x + i * (barW + gap);
    int by = y + maxH - h;
    tft.fillRect(bx, by, barW, h, (i < bars) ? onColor : 0x39C7);
  }
}

void drawBrandAt(int x, int cy, uint16_t bg, uint8_t font) {
  const char *left = "tab";
  const char *mid = "b";
  const char *right = "y-touch";
  int w1 = tft.textWidth(left, font);
  int w2 = tft.textWidth(mid, font);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(left, x, cy, font);
  tft.setTextColor(TFT_RED, bg);
  tft.drawString(mid, x + w1, cy, font);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(right, x + w1 + w2, cy, font);
  tft.setTextDatum(TL_DATUM);
}

void drawBrand(int cx, int cy, uint16_t bg, uint8_t font) {
  const int w = tft.textWidth("tabby-touch", font);
  drawBrandAt(cx - w / 2, cy, bg, font);
}

void drawHudCorners(int x, int y, int w, int h, uint16_t c) {
  const int L = 14;
  tft.drawFastHLine(x, y, L, c);
  tft.drawFastVLine(x, y, L, c);
  tft.drawFastHLine(x + w - L, y, L, c);
  tft.drawFastVLine(x + w - 1, y, L, c);
  tft.drawFastHLine(x, y + h - 1, L, c);
  tft.drawFastVLine(x, y + h - L, L, c);
  tft.drawFastHLine(x + w - L, y + h - 1, L, c);
  tft.drawFastVLine(x + w - 1, y + h - L, L, c);
}

void drawMacLine(int y, uint16_t bg) {
  char macLine[32];
  snprintf(macLine, sizeof(macLine), "MAC %s", WiFi.macAddress().c_str());
  tft.setTextColor(TFT_DARKGREY, bg);
  tft.drawString(macLine, 160, y, 2);
}

void showBootScreen(const char *statusLine, bool showSignal) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 48, TFT_NAVY);
  drawBrand(160, 24, TFT_NAVY, 4);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("WiFi", 160, 72, 4);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(statusLine, 160, 102, 2);

  if (showSignal && WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    drawSignalBars(140, 118, rssi, true);

    char ssidLine[48];
    snprintf(ssidLine, sizeof(ssidLine), "Ag: %s", WiFi.SSID().c_str());
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(ssidLine, 160, 156, 2);

    char rssiLine[32];
    snprintf(rssiLine, sizeof(rssiLine), "Sinyal: %d dBm", rssi);
    tft.setTextColor(wifiBarColor(wifiBarCount(rssi)), TFT_BLACK);
    tft.drawString(rssiLine, 160, 180, 2);
  }

  drawMacLine(220, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
}

bool connectWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, true);
  delay(100);

  wifi_config_t cfg = {};
  strlcpy(reinterpret_cast<char *>(cfg.sta.ssid), WIFI_SSID, sizeof(cfg.sta.ssid));
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg.sta.pmf_cfg.capable = false;
  cfg.sta.pmf_cfg.required = false;
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_wifi_connect();

  char first[48];
  snprintf(first, sizeof(first), "Baglaniyor: %s", WIFI_SSID);
  showBootScreen(first, false);

  uint32_t start = millis();
  uint8_t dots = 0;
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(400);
    dots = (dots + 1) % 4;
    char msg[56];
    snprintf(msg, sizeof(msg), "Baglaniyor: %s%.*s",
             WIFI_SSID, dots, "...");
    tft.setTextDatum(MC_DATUM);
    tft.fillRect(0, 100, 320, 28, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(msg, 160, 112, 2);
    tft.setTextDatum(TL_DATUM);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    showBootScreen("Baglandi", true);
    Serial.printf("WiFi OK SSID=%s RSSI=%d dBm IP=%s MAC=%s\n",
                  WiFi.SSID().c_str(),
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.macAddress().c_str());
    delay(2500);
    return true;
  }

  showBootScreen("Baglanamadi", false);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Gizli ag: TELETIP", 160, 160, 2);
  tft.setTextDatum(TL_DATUM);
  Serial.println("WiFi FAIL (hidden open TELETIP)");
  delay(2000);
  return false;
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 40, COL_HDR);
  tft.fillRect(0, 40, 320, 2, COL_ACCENT);
  drawBrandAt(8, 20, COL_HDR, 4);

  bool wifiOk = WiFi.status() == WL_CONNECTED;
  int rssi = wifiOk ? WiFi.RSSI() : 0;
  drawSignalBars(232, 12, rssi, wifiOk);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(wifiOk ? COL_ACCENT : TFT_RED, COL_HDR);
  tft.drawString(wifiOk ? WiFi.SSID() : "OFF", 264, 20, 2);
  tft.setTextDatum(TL_DATUM);
}

String jsonText(const JsonDocument &doc, const char *key) {
  JsonVariantConst v = doc[key];
  if (v.isNull()) return "";
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    return (s && s[0]) ? String(s) : "";
  }
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return String(v.as<long>());
  }
  if (v.is<long long>()) {
    return String((long)v.as<long long>());
  }
  String s = v.as<String>();
  return s;
}

bool jsonBool(const JsonDocument &doc, const char *key) {
  JsonVariantConst v = doc[key];
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return v.as<long>() != 0;
  }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (!s || !s[0]) return false;
    return strcmp(s, "1") == 0 || strcasecmp(s, "true") == 0;
  }
  String s = v.as<String>();
  s.toLowerCase();
  return s == "1" || s == "true";
}

int jsonInt(const JsonDocument &doc, const char *key, int def = 0) {
  JsonVariantConst v = doc[key];
  if (v.isNull()) return def;
  if (v.is<int>() || v.is<long>() || v.is<unsigned int>() || v.is<unsigned long>()) {
    return v.as<int>();
  }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    return (s && s[0]) ? atoi(s) : def;
  }
  return def;
}

void drawButton(const Btn &b, uint16_t fill, const char *label, uint8_t font = 4) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 10, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 10, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2, font);
  tft.setTextDatum(TL_DATUM);
}

void clearPinBuf() {
  pinLen = 0;
  pinBuf[0] = 0;
}

void wipeSession() {
  clearPinBuf();
  currentRegno = "";
  currentPin = "";
  currentName = "";
  pinDeadlineMs = 0;
}

void showStatus(const char *title, const char *detail) {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(title, 160, 100, 4);
  if (detail && detail[0]) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextWrap(true);
    int y = 140;
    String msg = detail;
    while (msg.length()) {
      int cut = 26;
      if ((int)msg.length() <= cut) {
        tft.drawString(msg, 160, y, 2);
        break;
      }
      int space = msg.lastIndexOf(' ', cut);
      if (space < 10) space = cut;
      tft.drawString(msg.substring(0, space), 160, y, 2);
      msg = msg.substring(space);
      msg.trim();
      y += 22;
      if (y > 210) break;
    }
  }
  tft.setTextDatum(TL_DATUM);
}

void drawPinDisplay() {
  const bool pinPad = false;
  const uint16_t fill = pinPad ? KEY_ORANGE : TFT_NAVY;
  const uint16_t border = pinPad ? TFT_YELLOW : TFT_CYAN;
  tft.fillRoundRect(8, 8, 304, 42, 8, fill);
  tft.drawRoundRect(8, 8, 304, 42, 8, border);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);

  if (pinPad) {
    const int gap = 28;
    const int r = 8;
    int startX = 160 - ((PIN_LEN - 1) * gap) / 2;
    for (int i = 0; i < PIN_LEN; i++) {
      int cx = startX + i * gap;
      if (i < pinLen) tft.fillCircle(cx, 29, r, TFT_WHITE);
      else tft.drawCircle(cx, 29, r, TFT_WHITE);
    }
  } else if (pinLen == 0) {
    tft.setTextColor(TFT_DARKGREY, fill);
    tft.drawString(inputPromptStr, 160, 29, 2);
  } else {
    tft.drawString(pinBuf, 160, 29, 4);
  }
  tft.setTextDatum(TL_DATUM);
}

void drawIdleQr() {
  const int frameX = 10;
  const int frameY = 52;
  const int frameW = 156;
  const int frameH = 156;
  tft.fillRoundRect(frameX, frameY, frameW, frameH, 6, COL_CARD);
  tft.drawRoundRect(frameX, frameY, frameW, frameH, 6, COL_ACCENT);
  drawHudCorners(frameX + 3, frameY + 3, frameW - 6, frameH - 6, COL_ACCENT);

  const int pad = 14;
  const int boxX = frameX + pad;
  const int boxY = frameY + pad;
  const int boxW = frameW - pad * 2;
  const int boxH = frameH - pad * 2;
  tft.fillRect(boxX, boxY, boxW, boxH, TFT_WHITE);

  if (!qrKod[0]) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
    tft.drawString("SCAN", boxX + boxW / 2, boxY + boxH / 2 - 10, 2);
    tft.drawString("bekleniyor", boxX + boxW / 2, boxY + boxH / 2 + 10, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  QRCode qr;
  uint8_t qrBuf[qrcode_getBufferSize(3)];
  if (qrcode_initText(&qr, qrBuf, 3, ECC_LOW, qrKod) != 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_WHITE);
    tft.drawString("QR hata", boxX + boxW / 2, boxY + boxH / 2, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  const int scale = 4;
  const int size = qr.size * scale;
  const int x0 = boxX + (boxW - size) / 2;
  const int y0 = boxY + (boxH - size) / 2;
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        tft.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, TFT_BLACK);
      }
    }
  }
}

void showIdle() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawIdleQr();

  tft.fillRoundRect(176, 52, 134, 156, 6, COL_CARD);
  tft.drawRoundRect(176, 52, 134, 156, 6, 0x3D3F);
  drawHudCorners(180, 56, 126, 148, COL_ACCENT);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_CARD);
  tft.drawString("PDKS", 243, 72, 2);
  tft.setTextColor(TFT_WHITE, COL_CARD);
  tft.drawString("DOKUN", 243, 108, 4);
  tft.setTextColor(COL_MUTED, COL_CARD);
  tft.drawString("Sicil gir", 243, 142, 2);
  tft.drawString("QR 10 sn", 243, 164, 2);

  tft.fillRect(0, 218, 320, 22, COL_HDR);
  tft.setTextColor(COL_MUTED, COL_HDR);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(FIRMWARE_VERSION, 10, 229, 2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(WiFi.macAddress(), 310, 229, 2);
  tft.setTextDatum(TL_DATUM);
  qrDirty = false;
}

void showKeypad() {
  const bool pinPad = false;
  const uint16_t digitFill = pinPad ? KEY_ORANGE : KEY_BLUE;
  tft.fillScreen(TFT_BLACK);
  drawPinDisplay();
  for (int i = 0; i < 12; i++) {
    uint16_t fill = digitFill;
    uint8_t font = 4;
    if (keys[i].kind == KeyKind::Del) {
      fill = TFT_MAROON;
      font = 2;
    } else if (keys[i].kind == KeyKind::Ok) {
      fill = TFT_DARKGREEN;
      font = 2;
    }
    drawButton(keys[i].b, fill, keys[i].label, font);
  }
}

void showWaitAction() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Sicil", 12, 44, 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(currentRegno, 80, 44, 2);

  tft.setTextDatum(MC_DATUM);
  if (currentName.length()) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(currentName, 160, 78, 2);
  }
  tft.setTextDatum(TL_DATUM);

  drawButton(BTN_PLUS, COL_PLUS, "+", 6);
  drawButton(BTN_MINUS, COL_MINUS, "-", 6);
}

void showDone() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  uint16_t color = lastOk ? TFT_GREEN : TFT_ORANGE;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(lastOk ? "Hosgeldiniz" : "Gecis yok", 160, 70, 4);

  if (currentName.length()) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(currentName, 160, 110, 2);
  }

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextWrap(true);
  int y = 145;
  String msg = lastMessage;
  while (msg.length()) {
    int cut = 26;
    if ((int)msg.length() <= cut) {
      tft.drawString(msg, 160, y, 2);
      break;
    }
    int space = msg.lastIndexOf(' ', cut);
    if (space < 10) space = cut;
    tft.drawString(msg.substring(0, space), 160, y, 2);
    msg = msg.substring(space);
    msg.trim();
    y += 22;
    if (y > 200) break;
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Yeni islem icin bekleniyor...", 160, 220, 2);
  tft.setTextDatum(TL_DATUM);
}

void enterState(AppState s) {
  state = s;
  stateSinceMs = millis();
  lastKeyMs = millis();
  touchDown = true;
  ignoreTouchUntilMs = millis() + TOUCH_COOLDOWN_MS;

  if (s == AppState::Idle) {
    wipeSession();
    showIdle();
  } else if (s == AppState::KeypadRegno) {
    inputMaxLen = REGNO_LEN;
    inputMask = false;
    inputPromptStr = "Sicil no (6 hane)";
    currentPin = "";
    currentName = "";
    pinDeadlineMs = 0;
    clearPinBuf();
    showKeypad();
  } else {
    showDone();
  }
}

String firstRegno(const JsonDocument &doc) {
  const char *keys[] = {"regno", "regNo", "personelRegNo", "sicilNo", "sicil"};
  for (const char *k : keys) {
    String v = jsonText(doc, k);
    if (v.length()) return v;
  }
  return "";
}

ApiResult parseApi(int httpCode, const String &body) {
  ApiResult r;
  r.httpCode = httpCode;
  if (httpCode < 0) {
    r.message = "Baglanti hatasi";
    return r;
  }

  String trimmed = body;
  trimmed.trim();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, trimmed);
  if (err) {
    r.message = trimmed.length() ? utf8ToAsciiTr(trimmed) : "Sunucu yaniti okunamadi";
    Serial.printf("JSON err: %s\n", err.c_str());
    return r;
  }

  r.ok = jsonBool(doc, "ok");
  r.success = jsonBool(doc, "success");
  r.found = jsonBool(doc, "found");
  r.verified = jsonBool(doc, "verified");
  r.pinBekleniyor = jsonBool(doc, "pinBekleniyor");
  r.kayit = jsonBool(doc, "kayit");
  r.yetkili = jsonBool(doc, "yetkili") || jsonBool(doc, "izin") ||
              jsonBool(doc, "izinli") || jsonBool(doc, "guvenliListe") ||
              jsonBool(doc, "yetki") || jsonBool(doc, "yetkiVar");
  r.oturumId = jsonInt(doc, "oturumId");
  r.ttlSaniye = jsonInt(doc, "ttlSaniye");
  r.regno = firstRegno(doc);
  r.isim = utf8ToAsciiTr(jsonText(doc, "isim"));
  r.soyisim = utf8ToAsciiTr(jsonText(doc, "soyisim"));
  r.adSoyad = utf8ToAsciiTr(jsonText(doc, "adSoyad"));
  r.message = utf8ToAsciiTr(jsonText(doc, "message"));
  r.yon = utf8ToAsciiTr(jsonText(doc, "yon"));
  if (!r.adSoyad.length() && (r.isim.length() || r.soyisim.length())) {
    r.adSoyad = r.isim;
    if (r.soyisim.length()) {
      if (r.adSoyad.length()) r.adSoyad += " ";
      r.adSoyad += r.soyisim;
    }
  }
  return r;
}

void applyPerson(const ApiResult &r) {
  if (r.adSoyad.length()) currentName = r.adSoyad;
  if (r.regno.length()) currentRegno = r.regno;
}

bool messageWantsRestart(const String &message) {
  String m = message;
  m.toLowerCase();
  return m.indexOf("yeniden girin") >= 0 ||
         m.indexOf("gecerli bir onay kodu yok") >= 0 ||
         m.indexOf("cok fazla") >= 0;
}

bool lockHttp(uint32_t ms) {
  if (!httpMutex) return true;
  return xSemaphoreTake(httpMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void unlockHttp() {
  if (httpMutex) xSemaphoreGive(httpMutex);
}

String cihazMac() {
  return WiFi.macAddress();
}

String komutGetUrl() {
  String mac = cihazMac();
  mac.replace(":", "");
  return String(API_KOMUT_URL) + "?cihazId=" + CIHAZ_ID + "&mac=" + mac;
}

String komutHeartbeatJson(bool alindi, int komutId) {
  String json = String("{\"cihazId\":\"") + CIHAZ_ID +
                "\",\"mac\":\"" + cihazMac() +
                "\",\"online\":true,\"surum\":\"" + FIRMWARE_VERSION + "\"";
  if (alindi) {
    json += ",\"alindi\":true";
    if (komutId > 0) {
      json += ",\"komutId\":";
      json += String(komutId);
    }
  }
  json += "}";
  return json;
}

void noteOtaFromDoc(const JsonDocument &doc) {
  if (!(jsonBool(doc, "guncelle") || jsonBool(doc, "update") ||
        jsonBool(doc, "ota") || jsonBool(doc, "firmwareUpdate"))) {
    return;
  }
  String ver = jsonText(doc, "surum");
  if (!ver.length()) ver = jsonText(doc, "version");
  if (ver.length() && ver == FIRMWARE_VERSION) return;

  String url = jsonText(doc, "url");
  if (!url.length()) url = jsonText(doc, "firmwareUrl");
  if (!url.length()) url = jsonText(doc, "otaUrl");
  if (url.length() < 12 || url.length() >= sizeof(pendingOtaUrl)) return;

  strncpy(pendingOtaUrl, url.c_str(), sizeof(pendingOtaUrl) - 1);
  pendingOtaUrl[sizeof(pendingOtaUrl) - 1] = 0;
  pendingOta = true;
  Serial.printf("OTA teklif surum=%s\n", ver.c_str());
}

bool komutIcerigiAc(const JsonDocument &doc) {
  if (jsonBool(doc, "serbestBirak") || jsonBool(doc, "serbest_birak") ||
      jsonBool(doc, "serbest") || jsonBool(doc, "ac") || jsonBool(doc, "open") ||
      jsonBool(doc, "kapiAc") || jsonBool(doc, "roleCek") || jsonBool(doc, "pulse") ||
      jsonBool(doc, "manuelAc") || jsonBool(doc, "manuel_ac") ||
      jsonBool(doc, "kapiyiAc") || jsonBool(doc, "kirmiziKod") ||
      jsonBool(doc, "kirmizi_kod") || jsonBool(doc, "kirmizi") ||
      jsonBool(doc, "redCode") || jsonBool(doc, "alarm")) {
    return true;
  }
  String k = jsonText(doc, "komut");
  k.toLowerCase();
  k.replace("_", "");
  k.replace("-", "");
  k.replace(" ", "");
  return k == "ac" || k == "serbest" || k == "serbestbirak" || k == "open" ||
         k == "pulse" || k == "kapi" || k == "role" || k == "manuel" ||
         k == "manuelac" || k == "kirmizi" || k == "kirmizikod" || k == "alarm";
}

bool httpJson(const String &url, const String *postBody, int timeoutMs,
              int &code, String &body) {
  if (WiFi.status() != WL_CONNECTED) {
    code = -1;
    body = "";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(8);

  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs);
  if (!http.begin(client, url)) {
    code = -1;
    body = "";
    return false;
  }
  if (postBody) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(*postBody);
  } else {
    code = http.GET();
  }
  body = http.getString();
  http.end();
  return code > 0;
}

bool postTabby(const String &payload, ApiResult &out) {
  if (!lockHttp(API_TIMEOUT_MS + 2000)) {
    out.httpCode = -1;
    out.message = "Sunucu mesgul";
    return false;
  }
  int code = 0;
  String body;
  bool sent = httpJson(API_GECIS_URL, &payload, API_TIMEOUT_MS, code, body);
  if (!sent || code == 404) {
    sent = httpJson(API_URL, &payload, API_TIMEOUT_MS, code, body);
  }
  unlockHttp();
  if (!sent && code < 0) {
    out.httpCode = -1;
    out.message = "WiFi yok";
    return false;
  }
  Serial.printf("POST gecis/tabby-touch HTTP %d\n", code);
  out = parseApi(code, body);
  Serial.printf("ok=%d found=%d yetkili=%d kayit=%d\n",
                out.ok, out.found, out.yetkili, out.kayit);
  return code > 0;
}

bool getKod(const String &url, ApiResult &out) {
  if (!lockHttp(API_TIMEOUT_MS + 2000)) {
    out.httpCode = -1;
    out.message = "Sunucu mesgul";
    return false;
  }
  int code = 0;
  String body;
  bool sent = httpJson(url, nullptr, API_TIMEOUT_MS, code, body);
  unlockHttp();
  if (!sent && code < 0) {
    out.httpCode = -1;
    out.message = "WiFi yok";
    return false;
  }
  Serial.printf("GET /tabby-touch/kod HTTP %d\n", code);
  out = parseApi(code, body);
  Serial.printf("ok=%d verified=%d kayit=%d yon=%s\n",
                out.ok, out.verified, out.kayit, out.yon.c_str());
  return code > 0;
}

void sendDeviceHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  String payload = String("{\"cihazId\":\"") + CIHAZ_ID +
                   "\",\"mac\":\"" + cihazMac() +
                   "\",\"ip\":\"" + WiFi.localIP().toString() +
                   "\",\"online\":true,\"surum\":\"" + FIRMWARE_VERSION + "\"}";
  int code = 0;
  String body;
  httpJson(API_HEARTBEAT_URL, &payload, HEARTBEAT_TIMEOUT_MS, code, body);
  Serial.printf("Heartbeat %s ip=%s v=%s HTTP %d\n",
                cihazMac().c_str(),
                WiFi.localIP().toString().c_str(),
                FIRMWARE_VERSION, code);
  if (code < 200 || code >= 300) return;
  JsonDocument doc;
  if (!deserializeJson(doc, body)) noteOtaFromDoc(doc);
}

void fetchQrOnce() {
  if (WiFi.status() != WL_CONNECTED) return;
  String payload = String("{\"cihazId\":\"") + CIHAZ_ID +
                   "\",\"mac\":\"" + cihazMac() + "\"}";
  int code = 0;
  String body;
  if (!httpJson(API_QR_URL, &payload, QR_TIMEOUT_MS, code, body)) return;
  if (code < 200 || code >= 300) {
    Serial.printf("QR HTTP %d\n", code);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  String kod = jsonText(doc, "kod");
  if (!kod.length()) kod = jsonText(doc, "qrKod");
  if (!kod.length()) kod = jsonText(doc, "qr");
  if (!kod.length()) kod = jsonText(doc, "code");
  if (!kod.length()) kod = jsonText(doc, "token");
  kod.trim();
  if (kod.length() < 16 || kod.length() > 30) {
    Serial.printf("QR kod uzunluk %d\n", kod.length());
    return;
  }
  strncpy(qrKod, kod.c_str(), sizeof(qrKod) - 1);
  qrKod[sizeof(qrKod) - 1] = 0;
  qrDirty = true;
}

void maybeRefreshQr() {
  const uint32_t now = millis();
  if (lastQrMs != 0 &&
      (int32_t)(now - lastQrMs) < (int32_t)QR_REFRESH_MS) {
    return;
  }
  if (!lockHttp(80)) return;
  lastQrMs = now;
  fetchQrOnce();
  unlockHttp();
}

void runPendingOta() {
  if (!pendingOta || !pendingOtaUrl[0]) return;
  pendingOta = false;
  showStatus("Guncelleniyor", FIRMWARE_VERSION);
  Serial.printf("OTA basliyor %s\n", pendingOtaUrl);
  if (!lockHttp(2000)) {
    showStatus("Guncelleme", "Sunucu mesgul");
    delay(1500);
    if (state == AppState::Idle) showIdle();
    return;
  }
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(client, pendingOtaUrl);
  unlockHttp();
  pendingOtaUrl[0] = 0;
  if (ret == HTTP_UPDATE_OK) return;
  Serial.printf("OTA hata %d\n", (int)ret);
  showStatus("Guncelleme hata", "Tekrar denenecek");
  delay(2000);
  if (state == AppState::Idle) showIdle();
}

void maybeSendHeartbeat() {
  const uint32_t now = millis();
  if (lastHeartbeatMs != 0 &&
      (int32_t)(now - lastHeartbeatMs) < (int32_t)HEARTBEAT_MS) {
    return;
  }
  if (!lockHttp(80)) return;
  lastHeartbeatMs = now;
  sendDeviceHeartbeat();
  unlockHttp();
}

void ackKomut(int komutId) {
  String payload = komutHeartbeatJson(true, komutId);
  int code = 0;
  String body;
  httpJson(API_KOMUT_URL, &payload, KOMUT_TIMEOUT_MS, code, body);
}

bool pollKomutOnce() {
  int code = 0;
  String body;
  String payload = komutHeartbeatJson(false, 0);
  bool sent = httpJson(API_KOMUT_URL, &payload, KOMUT_TIMEOUT_MS, code, body);
  if (!sent || code == 404) {
    sent = httpJson(komutGetUrl(), nullptr, KOMUT_TIMEOUT_MS, code, body);
  }
  if (code == 404) {
    komutEndpointVar = false;
    if (lastKomut404LogMs == 0 || millis() - lastKomut404LogMs > 60000) {
      lastKomut404LogMs = millis();
      Serial.println("Komut ucu yok: POST/GET /tabby-touch/komut");
    }
    return false;
  }
  komutEndpointVar = true;
  if (code < 0) return true;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return true;
  noteOtaFromDoc(doc);
  if (!komutIcerigiAc(doc)) return true;

  pendingRemoteOpen = true;
  int komutId = jsonInt(doc, "komutId");
  ackKomut(komutId);
  Serial.printf("Uzaktan serbest komutId=%d\n", komutId);
  return true;
}

void komutTask(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(4000));
  lastHeartbeatMs = 0;
  for (;;) {
    uint32_t wait = KOMUT_POLL_MS;
    if (WiFi.status() == WL_CONNECTED) {
      maybeSendHeartbeat();
      maybeRefreshQr();
      if (lockHttp(80)) {
        bool endpointOk = pollKomutOnce();
        unlockHttp();
        if (!endpointOk) wait = 8000;
      }
    } else {
      wait = 4000;
    }
    vTaskDelay(pdMS_TO_TICKS(wait));
  }
}

void applyRemoteOpen() {
  openDoor("Sunucu");
}

bool buttonPressedNow() {
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  const uint32_t now = millis();
  if (pressed != buttonDown) {
    buttonDown = pressed;
    buttonChangedMs = now;
    if (!pressed) buttonFiredThisHold = false;
  }
  if (!pressed || buttonFiredThisHold) return false;
  if ((int32_t)(now - buttonChangedMs) < (int32_t)BUTTON_DEBOUNCE_MS) return false;
  if ((int32_t)(now - buttonIgnoreUntilMs) < 0) return false;
  buttonFiredThisHold = true;
  buttonIgnoreUntilMs = now + BUTTON_COOLDOWN_MS;
  Serial.println("IO35 GND: IO27 tetik");
  return true;
}

void serviceButton() {
  if (!buttonPressedNow()) return;
  Serial.println("Fiziksel buton: role");
  openDoor("Buton");
}

void finishWith(bool ok, const String &message) {
  lastOk = ok;
  lastMessage = utf8ToAsciiTr(message);
  lastTimeStr = nowTimeStr();
  doneShowMs = ok ? DONE_OK_MS : DONE_ERR_MS;
  if (ok) relayPulse();
  enterState(AppState::Done);
}

void submitSicil() {
  if (pinLen == 0) return;
  currentRegno = pinBuf;
  showStatus("Sicil kontrol...", currentRegno.c_str());

  String payload = String("{\"regno\":\"") + currentRegno + "\"}";
  Serial.printf("Sicil POST regno=%s\n", currentRegno.c_str());
  ApiResult r;
  postTabby(payload, r);
  applyPerson(r);

  const bool found = r.found || currentName.length();
  const bool yetkili = r.yetkili || r.kayit;

  if (yetkili) {
    String msg = r.message;
    if (!msg.length() && currentName.length()) msg = currentName + " Hosgeldiniz";
    if (!msg.length()) msg = "Hosgeldiniz";
    finishWith(true, msg);
    return;
  }
  if (found) {
    String msg = r.message;
    if (!msg.length() && currentName.length()) {
      msg = currentName + " Yetkiniz Bulunmamaktadir";
    }
    if (!msg.length()) msg = "Yetkiniz bulunmamaktadir.";
    finishWith(false, msg);
    return;
  }
  finishWith(false, r.message.length() ? r.message
                                       : "Bu sicil numarasi sistemde tanimli degildir.");
}

void submitInput() {
  if (state == AppState::KeypadRegno) submitSicil();
}

void handleKey(const Key &k) {
  lastKeyMs = millis();
  if (k.kind == KeyKind::Digit) {
    if (pinLen >= inputMaxLen) return;
    pinBuf[pinLen++] = k.digit;
    pinBuf[pinLen] = 0;
    drawPinDisplay();
    if (state == AppState::KeypadRegno && pinLen == REGNO_LEN) {
      delay(80);
      submitSicil();
    }
    return;
  }
  if (k.kind == KeyKind::Del) {
    if (pinLen == 0) {
      enterState(AppState::Idle);
      return;
    }
    pinBuf[--pinLen] = 0;
    drawPinDisplay();
    return;
  }
  submitInput();
}

bool readTouch(int &x, int &y) {
  TouchPoint p = ts.getTouch();
  if (p.zRaw < TOUCH_Z_MIN) {
    touchDown = false;
    return false;
  }
  if (millis() < ignoreTouchUntilMs) return false;
  if (touchDown) return false;
  if (millis() - lastTouchMs < 180) return false;
  if (p.x < 0 || p.x > 319 || p.y < 0 || p.y > 239) return false;

  touchDown = true;
  lastTouchMs = millis();
  x = p.x;
  y = p.y;
  Serial.printf("Touch: %d,%d z=%d\n", x, y, p.zRaw);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\ntabby-touch %s — sicil + QR + OTA\n", FIRMWARE_VERSION);

  setenv("TZ", "Europe/Istanbul", 1);
  tzset();

  initKeys();

  tft.init();
  tft.setRotation(1);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  relayOff();
  pinMode(BUTTON_PIN, INPUT);
  buttonDown = digitalRead(BUTTON_PIN) == LOW;
  buttonFiredThisHold = false;
  buttonChangedMs = millis();

  ts.begin();
  ts.setCalibration(180, 3700, 280, 3800);

  httpMutex = xSemaphoreCreateMutex();
  connectWifi();
  xTaskCreatePinnedToCore(komutTask, "komut", 12288, nullptr, 1, nullptr, 0);
  enterState(AppState::Idle);
}

void loop() {
  serviceRelay();
  serviceButton();
  if (pendingOta) runPendingOta();
  if (pendingRemoteOpen) {
    pendingRemoteOpen = false;
    applyRemoteOpen();
  }
  if (qrDirty && state == AppState::Idle && remoteBannerUntilMs == 0) {
    drawIdleQr();
    qrDirty = false;
  }
  if (remoteBannerUntilMs != 0 &&
      (int32_t)(millis() - remoteBannerUntilMs) >= 0) {
    remoteBannerUntilMs = 0;
    if (state == AppState::Idle) showIdle();
  }

  int x, y;
  switch (state) {
    case AppState::Idle:
      if (readTouch(x, y)) enterState(AppState::KeypadRegno);
      break;

    case AppState::KeypadRegno:
      if (millis() - lastKeyMs > KEYPAD_TIMEOUT_MS) {
        enterState(AppState::Idle);
        break;
      }
      if (readTouch(x, y)) {
        for (int i = 0; i < 12; i++) {
          if (hitTest(keys[i].b, x, y)) {
            handleKey(keys[i]);
            break;
          }
        }
      }
      break;

    case AppState::Done:
      if (millis() - stateSinceMs > doneShowMs) {
        enterState(AppState::Idle);
      }
      break;
  }

  delay(20);
}
