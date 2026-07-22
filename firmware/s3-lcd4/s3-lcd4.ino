// =====================================================================
//  fen.dog badge — Waveshare ESP32-S3-Touch-LCD-4 (480x480, ST7701 RGB)
//  Swipeable screens: BADGE / FURSONA / BOOP / QR / MESSAGES
//  WiFi: captive portal "FEN-BADGE" on first boot (WiFiManager)
//  Syncs with the live hub: https://fen.188.166.49.216.sslip.io
//    - polls /api/poll every 4 s (presence + boop total + latest message)
//    - tap the BOOP screen -> POST /api/boop (shows on the phone PWA live)
//  Pinout & init per Waveshare official demo (V4.0 board, CH32V003 io-exp).
// =====================================================================
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "WS_CH32_IO.h"
#include "img_fen_220.h"
#include "img_qr_300.h"

// ---------- hub ----------
#define HUB_HOST "fen.188.166.49.216.sslip.io"
#define DEVICE_ID "lcd4"
#define FW_VER "1.0"

// ---------- fen.dog palette (RGB565) ----------
#define C_BG     0x0000
#define C_INK    0xF79D
#define C_DIM    0x7BEF
#define C_TQ     0x7E79   // #78CDCF
#define C_GOLD   0xFEA6   // #ffd737
#define C_PURPLE 0xB25F   // #b24bff
#define C_PINK   0xFC79   // #ff8cc8

// ---------- display (Waveshare demo pins, V4.0) ----------
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /*DC*/, 42 /*CS*/, 2 /*SCK*/, 1 /*MOSI*/, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40 /*DE*/, 39 /*VSYNC*/, 38 /*HSYNC*/, 41 /*PCLK*/,
    46, 3, 8, 18, 17,          // R0-R4
    14, 13, 12, 11, 10, 9,     // G0-G5
    5, 45, 48, 47, 21,         // B0-B4
    1, 10, 8, 50,              // hsync pol/fp/pw/bp
    1, 10, 8, 20);             // vsync pol/fp/pw/bp
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 2 /*rotation: 180° — matches lanyard orientation*/, true,
    bus, GFX_NOT_DEFINED /*RST*/,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

// ---------- touch (GT911 via SensorLib; reset handled by CH32 expander) ----------
#include "TouchDrvGT911.hpp"
TouchDrvGT911 touch;
bool touchOk = false;

// ---------- state ----------
int screen = 0;                 // 0 badge · 1 fursona · 2 boop · 3 qr · 4 msg
const int NSCREENS = 5;
long boopTotal = -1;
String lastMsg = "", lastMsgFrom = "";
bool wifiOk = false;
unsigned long lastPoll = 0, lastDraw = 0;
int16_t tDownX = -1, tDownY = -1; bool tDown = false;

// =====================================================================
void drawHeader(const char *title) {
  gfx->fillRect(0, 0, 480, 54, C_BG);
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(16, 12); gfx->print("fen.dog");
  gfx->setTextColor(C_DIM);
  gfx->setCursor(16, 34); gfx->print(title);
  // wifi dot
  gfx->fillCircle(456, 20, 7, wifiOk ? 0x4FE9 /*green*/ : 0xF800 /*red*/);
  // holo divider
  const uint16_t seg[6] = {C_TQ, 0x6EFC, C_PURPLE, C_PINK, C_GOLD, C_TQ};
  for (int i = 0; i < 6; i++) gfx->fillRect(i * 80, 52, 80, 3, seg[i]);
  // swipe dots
  for (int i = 0; i < NSCREENS; i++)
    gfx->fillCircle(480/2 - (NSCREENS-1)*11 + i*22, 468, 5, i == screen ? C_GOLD : 0x2965);
}

void drawBadge() {
  gfx->fillScreen(C_BG); drawHeader("AWOOSTRIA 2026 - VIENNA");
  gfx->setTextColor(C_INK); gfx->setTextSize(10);
  gfx->setCursor(64, 140); gfx->print("FEN");
  gfx->setTextSize(3); gfx->setTextColor(C_TQ);
  gfx->setCursor(64, 240); gfx->print("a silly holo pupper");
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(64, 290); gfx->print("SERVICE PUP  ·  OMEGA");
  gfx->fillRect(64, 330, 352, 2, C_PURPLE);
  gfx->setTextColor(C_GOLD); gfx->setTextSize(3);
  gfx->setCursor(64, 360); gfx->print("BOOP ME! ->");
  gfx->setTextColor(C_DIM); gfx->setTextSize(2);
  gfx->setCursor(64, 410); gfx->print("swipe for more");
}

void drawFursona() {
  gfx->fillScreen(C_BG); drawHeader("THE PUP HIMSELF");
  gfx->draw16bitRGBBitmap(130, 110, (uint16_t*)fen_art, FEN_ART_W, FEN_ART_H);
  gfx->setTextSize(3); gfx->setTextColor(C_TQ);
  gfx->setCursor(150, 370); gfx->print("hi, i'm Fen!");
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(115, 410); gfx->print("come say hi at the con :3");
}

void drawBoop() {
  gfx->fillScreen(C_BG); drawHeader("BOOP COUNTER");
  gfx->setTextSize(3); gfx->setTextColor(C_DIM);
  gfx->setCursor(140, 100); gfx->print("total boops");
  char buf[16]; snprintf(buf, sizeof buf, "%ld", boopTotal < 0 ? 0 : boopTotal);
  int len = strlen(buf);
  gfx->setTextSize(len > 4 ? 10 : 13);
  int cw = (len > 4 ? 60 : 78);
  gfx->setCursor(240 - len * cw / 2, 170);
  gfx->setTextColor(C_GOLD); gfx->print(buf);
  gfx->setTextSize(4); gfx->setTextColor(C_PINK);
  gfx->setCursor(96, 330); gfx->print("TAP TO BOOP!");
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(105, 400); gfx->print("(yes the screen is the button)");
}

void drawQR() {
  gfx->fillScreen(C_BG); drawHeader("SCAN TO BOOP ME");
  gfx->draw16bitRGBBitmap(90, 80, (uint16_t*)qr_img, QR_IMG_W, QR_IMG_H);
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(80, 410); gfx->print("boop counter + live messages");
}

void drawMsg() {
  gfx->fillScreen(C_BG); drawHeader("LIVE MESSAGES");
  if (lastMsg.length() == 0) {
    gfx->setTextSize(3); gfx->setTextColor(C_DIM);
    gfx->setCursor(90, 220); gfx->print("no messages yet");
  } else {
    gfx->setTextSize(4); gfx->setTextColor(C_INK);
    // crude word wrap at ~20 chars/line
    int y = 130; String line = "";
    for (unsigned i = 0; i <= lastMsg.length(); i++) {
      if (i == lastMsg.length() || (lastMsg[i] == ' ' && line.length() > 16)) {
        if (i < lastMsg.length()) line += lastMsg[i];
        gfx->setCursor(40, y); gfx->print(line); y += 44; line = "";
        if (y > 340) break;
      } else line += lastMsg[i];
    }
    gfx->setTextSize(2); gfx->setTextColor(C_GOLD);
    gfx->setCursor(40, 390); gfx->print("- "); gfx->print(lastMsgFrom);
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
  // expanding holo rings from the centre + a paw of dots — quick and cute
  const uint16_t cols[4] = {C_PINK, C_PURPLE, C_TQ, C_GOLD};
  for (int r = 20; r <= 300; r += 28) {
    uint16_t c = cols[(r / 28) % 4];
    gfx->drawCircle(240, 240, r, c);
    gfx->drawCircle(240, 240, r + 1, c);
    gfx->drawCircle(240, 240, r + 2, c);
    delay(18);
  }
  // paw-print burst
  gfx->fillCircle(240, 258, 34, C_PINK);
  gfx->fillCircle(206, 216, 15, C_PINK); gfx->fillCircle(240, 204, 16, C_PINK);
  gfx->fillCircle(274, 216, 15, C_PINK);
  delay(220);
  draw();
}

// =====================================================================
void poll() {
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(4000);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/poll?device=" DEVICE_ID "&fw=" FW_VER)) {
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        long nb = doc["boops"] | -1;
        String nm = doc["msg"]["text"] | "";
        String nf = doc["msg"]["from"] | "";
        bool changed = false;
        if (nb != boopTotal) { if (boopTotal >= 0 && screen == 2) changed = true; boopTotal = nb;
          if (screen == 2) changed = true; }
        if (nm != lastMsg) { lastMsg = nm; lastMsgFrom = nf; if (screen == 4) changed = true; }
        if (changed) draw();
      }
    }
    http.end();
  }
}

void sendBoop() {
  boopTotal++;                       // optimistic
  boopFlash();
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(4000);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/boop")) {
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"device\":\"" DEVICE_ID "\",\"who\":\"badge tap\"}");
    http.end();
  }
}

// =====================================================================
void setup() {
  Serial.begin(115200);

  // CH32V003 IO expander MUST come up before the panel (powers + resets it)
  WS_CH32_IO::begin(Wire, WS_CH32_IO::DEFAULT_I2C_SDA, WS_CH32_IO::DEFAULT_I2C_SCL,
                    WS_CH32_IO::DEFAULT_I2C_FREQ, &Serial);
  gfx->begin();
  gfx->fillScreen(C_BG);

  // touch (reset already released by the expander)
  touch.setPins(-1, -1);
  touchOk = touch.begin(Wire, 0x5D, 15, 7) || touch.begin(Wire, 0x14, 15, 7);
  Serial.printf("touch: %s\n", touchOk ? "ok" : "FAILED");

  // wifi provisioning screen
  drawHeader("WIFI SETUP");
  gfx->setTextSize(3); gfx->setTextColor(C_INK);
  gfx->setCursor(40, 120); gfx->print("connect your phone to");
  gfx->setTextColor(C_TQ); gfx->setCursor(40, 170); gfx->print("WiFi: FEN-BADGE");
  gfx->setTextColor(C_INK); gfx->setCursor(40, 230); gfx->print("then pick the con WiFi");
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(40, 290); gfx->print("(skips this once it knows a network)");

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);          // give up after 3 min -> offline mode
  wifiOk = wm.autoConnect("FEN-BADGE");
  Serial.printf("wifi: %s\n", wifiOk ? WiFi.localIP().toString().c_str() : "offline");

  draw();
  if (wifiOk) poll();
}

void loop() {
  // ---- touch: swipe left/right = change screen, tap = boop (on boop screen) ----
  if (touchOk) {
    static int16_t lastX = 0, lastY = 0;
    int16_t x[1], y[1];
    uint8_t n = touch.getPoint(x, y, 1);
    if (n > 0) {
      x[0] = 479 - x[0]; y[0] = 479 - y[0];   // GT911 is panel-native; display runs rotation 2
      if (!tDown) { tDown = true; tDownX = x[0]; tDownY = y[0]; }   // finger down
      lastX = x[0]; lastY = y[0];                                    // track latest
    } else if (tDown) {                                              // finger up
      tDown = false;
      int dx = lastX - tDownX;
      if (abs(dx) > 70) {                                            // swipe
        screen = (screen + (dx < 0 ? 1 : NSCREENS - 1)) % NSCREENS;
        draw();
      } else if (screen == 2) {                                      // tap
        sendBoop();
      }
    }
  }

  // ---- periodic hub sync ----
  if (millis() - lastPoll > 4000) { lastPoll = millis(); poll(); }
  delay(20);
}
