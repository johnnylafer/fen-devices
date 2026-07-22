// =====================================================================
//  fen.dog pendant — Waveshare ESP32-C6-LCD-1.47 (172x320, ST7789 SPI)
//  Screens: BADGE / FURSONA / BOOP / QR / MESSAGES
//  No touch: BOOT button (GPIO9)  short press = next screen
//                                 long press (>600 ms) = BOOP!
//  Onboard WS2812 (GPIO8) flashes on boops.
//  WiFi: captive portal "FEN-PENDANT" on first boot (WiFiManager)
//  Hub:  https://fen.188.166.49.216.sslip.io  (poll 4 s / POST boop)
//  Pins & panel offsets per Waveshare official demo (offset X=34).
// =====================================================================
#include <Arduino_GFX_Library.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "img_fen_110.h"
#include "img_qr_130.h"

// ---------- hub ----------
#define HUB_HOST "fen.188.166.49.216.sslip.io"
#define DEVICE_ID "c6"
#define FW_VER "1.0"

// ---------- pins ----------
#define PIN_BL   22
#define PIN_BTN  9      // BOOT button, active low
#define PIN_RGB  8      // WS2812

// ---------- fen.dog palette (RGB565) ----------
#define C_BG     0x0000
#define C_INK    0xF79D
#define C_DIM    0x7BEF
#define C_TQ     0x7E79
#define C_GOLD   0xFEA6
#define C_PURPLE 0xB25F
#define C_PINK   0xFC79

// ---------- display: ST7789 172x320, offsets 34/0, IPS inversion ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    15 /*DC*/, 14 /*CS*/, 7 /*SCK*/, 6 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 21 /*RST*/, 0 /*rotation: portrait 172x320*/, true /*IPS*/,
    172, 320, 34, 0, 34, 0);

// ---------- state ----------
int screen = 0; const int NSCREENS = 5;
long boopTotal = -1;
String lastMsg = "", lastMsgFrom = "";
bool wifiOk = false;
unsigned long lastPoll = 0, btnDownAt = 0;
bool btnWasDown = false, longFired = false;

// =====================================================================
void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_RGB, r, g, b); }

void drawHeader(const char *title) {
  gfx->setTextSize(1); gfx->setTextColor(C_TQ);
  gfx->setCursor(8, 6); gfx->print("fen.dog");
  gfx->setTextColor(C_DIM);
  gfx->setCursor(8, 18); gfx->print(title);
  gfx->fillCircle(160, 10, 4, wifiOk ? 0x4FE9 : 0xF800);
  const uint16_t seg[4] = {C_TQ, C_PURPLE, C_PINK, C_GOLD};
  for (int i = 0; i < 4; i++) gfx->fillRect(i * 43, 30, 43, 2, seg[i]);
  for (int i = 0; i < NSCREENS; i++)
    gfx->fillCircle(172/2 - (NSCREENS-1)*8 + i*16, 312, 3, i == screen ? C_GOLD : 0x2965);
}

void drawBadge() {
  gfx->fillScreen(C_BG); drawHeader("AWOOSTRIA 2026");
  gfx->setTextSize(5); gfx->setTextColor(C_INK);
  gfx->setCursor(14, 70); gfx->print("FEN");
  gfx->setTextSize(1); gfx->setTextColor(C_TQ);
  gfx->setCursor(14, 130); gfx->print("a silly holo pupper");
  gfx->setTextColor(C_DIM);
  gfx->setCursor(14, 150); gfx->print("SERVICE PUP - OMEGA");
  gfx->fillRect(14, 172, 144, 1, C_PURPLE);
  gfx->setTextSize(2); gfx->setTextColor(C_GOLD);
  gfx->setCursor(14, 195); gfx->print("BOOP ME!");
  gfx->setTextSize(1); gfx->setTextColor(C_DIM);
  gfx->setCursor(14, 240); gfx->print("btn: short = next");
  gfx->setCursor(14, 254); gfx->print("     long  = boop");
}

void drawFursona() {
  gfx->fillScreen(C_BG); drawHeader("THE PUP HIMSELF");
  gfx->draw16bitRGBBitmap(31, 60, (uint16_t*)fen_art_sm, FEN_ART_SM_W, FEN_ART_SM_H);
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(26, 195); gfx->print("hi, Fen!");
  gfx->setTextSize(1); gfx->setTextColor(C_DIM);
  gfx->setCursor(22, 230); gfx->print("come say hi at the con");
}

void drawBoop() {
  gfx->fillScreen(C_BG); drawHeader("BOOP COUNTER");
  gfx->setTextSize(1); gfx->setTextColor(C_DIM);
  gfx->setCursor(52, 60); gfx->print("total boops");
  char buf[16]; snprintf(buf, sizeof buf, "%ld", boopTotal < 0 ? 0 : boopTotal);
  int len = strlen(buf);
  int ts = len > 4 ? 4 : 6;
  gfx->setTextSize(ts);
  gfx->setCursor(86 - len * ts * 3, 110);
  gfx->setTextColor(C_GOLD); gfx->print(buf);
  gfx->setTextSize(2); gfx->setTextColor(C_PINK);
  gfx->setCursor(14, 210); gfx->print("HOLD = BOOP");
}

void drawQR() {
  gfx->fillScreen(C_BG); drawHeader("SCAN TO BOOP");
  gfx->draw16bitRGBBitmap(21, 60, (uint16_t*)qr_img_sm, QR_IMG_SM_W, QR_IMG_SM_H);
  gfx->setTextSize(1); gfx->setTextColor(C_TQ);
  gfx->setCursor(18, 215); gfx->print("boops + live messages");
}

void drawMsg() {
  gfx->fillScreen(C_BG); drawHeader("LIVE MESSAGES");
  if (lastMsg.length() == 0) {
    gfx->setTextSize(1); gfx->setTextColor(C_DIM);
    gfx->setCursor(35, 150); gfx->print("no messages yet");
  } else {
    gfx->setTextSize(2); gfx->setTextColor(C_INK);
    int y = 60; String line = "";
    for (unsigned i = 0; i <= lastMsg.length(); i++) {
      if (i == lastMsg.length() || (lastMsg[i] == ' ' && line.length() > 11)) {
        if (i < lastMsg.length()) line += lastMsg[i];
        gfx->setCursor(10, y); gfx->print(line); y += 22; line = "";
        if (y > 250) break;
      } else line += lastMsg[i];
    }
    gfx->setTextSize(1); gfx->setTextColor(C_GOLD);
    gfx->setCursor(10, 280); gfx->print("- "); gfx->print(lastMsgFrom);
  }
}

void draw() {
  switch (screen) {
    case 0: drawBadge(); break;
    case 1: drawFursona(); break;
    case 2: drawBoop(); break;
    case 3: drawQR(); break;
    case 4: drawMsg(); break;
  }
}

void boopFlash() {
  led(255, 60, 160);
  gfx->fillScreen(C_PINK); delay(70);
  gfx->fillScreen(C_BG);
  led(0, 0, 0);
  draw();
}

// =====================================================================
void poll() {
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(4000);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/poll?device=" DEVICE_ID "&fw=" FW_VER)) {
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        long nb = doc["boops"] | -1;
        String nm = doc["msg"]["text"] | "";
        String nf = doc["msg"]["from"] | "";
        bool changed = false;
        if (nb != boopTotal) {
          if (boopTotal >= 0) { led(120, 255, 120); delay(60); led(0,0,0); }  // green blink on new boop
          boopTotal = nb; if (screen == 2) changed = true;
        }
        if (nm != lastMsg) { lastMsg = nm; lastMsgFrom = nf; if (screen == 4) changed = true; }
        if (changed) draw();
      }
    }
    http.end();
  }
}

void sendBoop() {
  boopTotal++;
  boopFlash();
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(4000);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/boop")) {
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"device\":\"" DEVICE_ID "\",\"who\":\"pendant\"}");
    http.end();
  }
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH);

  gfx->begin();
  gfx->fillScreen(C_BG);
  led(0, 20, 20);

  // wifi provisioning screen
  drawHeader("WIFI SETUP");
  gfx->setTextSize(1); gfx->setTextColor(C_INK);
  gfx->setCursor(10, 60);  gfx->print("connect phone to WiFi:");
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(10, 85);  gfx->print("FEN-");
  gfx->setCursor(10, 105); gfx->print("PENDANT");
  gfx->setTextSize(1); gfx->setTextColor(C_INK);
  gfx->setCursor(10, 150); gfx->print("then pick the con WiFi");
  gfx->setTextColor(C_DIM);
  gfx->setCursor(10, 175); gfx->print("(auto-skips once saved)");

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wifiOk = wm.autoConnect("FEN-PENDANT");
  led(0, 0, 0);

  draw();
  if (wifiOk) poll();
}

void loop() {
  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !btnWasDown) { btnWasDown = true; btnDownAt = millis(); longFired = false; }
  if (down && btnWasDown && !longFired && millis() - btnDownAt > 600) {
    longFired = true;                       // long press -> BOOP
    sendBoop();
  }
  if (!down && btnWasDown) {
    btnWasDown = false;
    if (!longFired && millis() - btnDownAt > 30) {   // short press -> next screen
      screen = (screen + 1) % NSCREENS;
      draw();
    }
  }

  if (millis() - lastPoll > 4000) { lastPoll = millis(); poll(); }
  delay(15);
}
