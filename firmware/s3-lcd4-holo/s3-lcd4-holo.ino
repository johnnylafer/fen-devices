// =====================================================================
//  fen.dog badge v2 "HOLO-MOTION" — ESP32-S3-Touch-LCD-4 (480x480)
//  Full-motion UI in fen.dog web styling: PSRAM canvas @ ~30 fps,
//  drifting holo particles, shimmering gradient text, animated wipes.
//
//  Screens (swipe left/right):
//    0 HOME      animated holo badge
//    1 FURSONA   art + particle halo
//    2 STICKERS  3x2 gallery, tap = zoom a sticker, tap again = back
//    3 FILM      polaroid filmstrip, bobbing
//    4 CINEMA    animated Fen GIF loop
//    5 BOOP      live counter, tap = boop (particle burst)
//    6 QR        scan-to-boop
//    7 MESSAGES  live from the hub
//
//  Same hub sync as v1 (poll 4 s, POST boop). WiFi portal "FEN-BADGE".
// =====================================================================
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include "WS_CH32_IO.h"
#include "img_fen_220.h"
#include "img_qr_300.h"
#include "img_stk1.h"
#include "img_stk2.h"
#include "img_stk3.h"
#include "img_stk4.h"
#include "img_stk5.h"
#include "img_stk6.h"
#include "img_pol1.h"
#include "img_pol2.h"
#include "img_fen_gif.h"

#define HUB_HOST "fen.188.166.49.216.sslip.io"
#define DEVICE_ID "lcd4"
#define FW_VER "2.0-holo"

// ---------- palette ----------
#define C_BG     0x0000
#define C_INK    0xF79D
#define C_DIM    0x7BEF
#define C_TQ     0x7E79
#define C_GOLD   0xFEA6
#define C_PURPLE 0xB25F
#define C_PINK   0xFC79
#define C_BLUE   0x6EFC
const uint16_t HOLO[6] = {C_TQ, C_BLUE, C_PURPLE, C_PINK, C_GOLD, C_TQ};

// ---------- display: canvas (rotated) over raw RGB panel ----------
Arduino_DataBus *ibus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, 42, 2, 1, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 39, 38, 41,
    46, 3, 8, 18, 17,
    14, 13, 12, 11, 10, 9,
    5, 45, 48, 47, 21,
    1, 10, 8, 50,
    1, 10, 8, 20);
Arduino_RGB_Display *disp = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /*panel native*/, true,
    ibus, GFX_NOT_DEFINED, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));
Arduino_Canvas *gfx = new Arduino_Canvas(480, 480, disp);   // PSRAM framebuffer

#include "TouchDrvGT911.hpp"
TouchDrvGT911 touch;
bool touchOk = false;

AnimatedGIF gif;
bool gifOpen = false;

// ---------- state ----------
int screen = 0; const int NSCREENS = 8;
int stickerSel = -1;                     // -1 = grid, 0..5 = zoomed
long boopTotal = -1;
String lastMsg = "", lastMsgFrom = "";
bool wifiOk = false;
unsigned long lastPoll = 0;
int16_t tDownX, tDownY; bool tDown = false;
float burst = 0;                          // boop burst animation 1→0

// particles
struct P { float x, y, vy, vx; uint8_t c, r; };
P parts[42];
void seedParts() {
  for (auto &p : parts) {
    p.x = random(480); p.y = random(480);
    p.vy = -(0.2f + random(100) / 160.0f);
    p.vx = (random(100) - 50) / 300.0f;
    p.c = random(5); p.r = 1 + random(3);
  }
}

const uint16_t *STK[6] = {stk1, stk2, stk3, stk4, stk5, stk6};

// =====================================================================
//                         drawing helpers
// =====================================================================
uint16_t holoAt(int i, uint32_t t) { return HOLO[((i + t / 120) % 6)]; }

void particles(uint32_t t, int fromY = 0) {
  for (auto &p : parts) {
    p.x += p.vx; p.y += p.vy;
    if (p.y < -4) { p.y = 484; p.x = random(480); }
    if (p.x < -4) p.x = 484; else if (p.x > 484) p.x = -4;
    if (p.y > fromY) gfx->fillCircle((int)p.x, (int)p.y, p.r, HOLO[p.c]);
  }
}

void scanlines() {
  for (int y = 0; y < 480; y += 5) gfx->drawFastHLine(0, y, 480, 0x0841);
}

void header(const char *title, uint32_t t) {
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(16, 10); gfx->print("fen.dog");
  gfx->setTextColor(C_DIM); gfx->setCursor(16, 32); gfx->print(title);
  gfx->fillCircle(458, 18, 6, wifiOk ? 0x4FE9 : 0xF800);
  for (int i = 0; i < 6; i++) gfx->fillRect(i * 80, 50, 80, 3, holoAt(i, t));
  for (int i = 0; i < NSCREENS; i++)
    gfx->fillCircle(240 - (NSCREENS - 1) * 10 + i * 20, 470, 4, i == screen ? C_GOLD : 0x2124);
}

void holoText(const char *s, int x, int y, int size, uint32_t t) {
  gfx->setTextSize(size);
  for (int i = 0; s[i]; i++) {
    gfx->setTextColor(holoAt(i, t));
    gfx->setCursor(x + i * 6 * size, y);
    gfx->print(s[i]);
  }
}

// =====================================================================
//                             screens
// =====================================================================
void scrHome(uint32_t t) {
  particles(t);
  header("AWOOSTRIA 2026 - VIENNA", t);
  float bob = sinf(t / 500.0f) * 6;
  holoText("FEN", 96, 140 + (int)bob, 14, t);
  gfx->setTextSize(3); gfx->setTextColor(C_TQ);
  gfx->setCursor(74, 280 + (int)(bob / 2)); gfx->print("a silly holo pupper");
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(120, 330); gfx->print("SERVICE PUP  ·  OMEGA");
  // boop chip
  gfx->drawRoundRect(130, 370, 220, 46, 12, C_GOLD);
  gfx->setTextSize(3); gfx->setTextColor(C_GOLD);
  gfx->setCursor(148, 381); gfx->print(boopTotal < 0 ? 0 : boopTotal);
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(240, 386); gfx->print("boops :3");
  // pulsing swipe hint
  if ((t / 600) % 2) { gfx->setTextColor(C_PINK); gfx->setCursor(180, 434); gfx->print("< swipe >"); }
}

void scrFursona(uint32_t t) {
  particles(t);
  header("THE PUP HIMSELF", t);
  float bob = sinf(t / 600.0f) * 5;
  gfx->draw16bitRGBBitmap(130, 105 + (int)bob, (uint16_t*)fen_art, FEN_ART_W, FEN_ART_H);
  // halo ring
  int r = 150 + (int)(sinf(t / 350.0f) * 6);
  gfx->drawCircle(240, 215 + (int)bob, r, holoAt(0, t));
  holoText("hi, i'm Fen!", 168, 370, 3, t);
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(122, 414); gfx->print("come say hi at the con :3");
}

void scrStickers(uint32_t t) {
  header("STICKER PICKER", t);
  if (stickerSel < 0) {
    for (int i = 0; i < 6; i++) {
      int gx = 45 + (i % 3) * 140, gy = 80 + (i / 3) * 160;
      gfx->draw16bitRGBBitmap(gx, gy, (uint16_t*)STK[i], 120, 120);
      // animated holo ring runs across the grid
      if ((t / 400) % 6 == i) gfx->drawRoundRect(gx - 4, gy - 4, 128, 128, 10, holoAt(i, t));
    }
    gfx->setTextSize(2); gfx->setTextColor(C_DIM);
    gfx->setCursor(140, 420); gfx->print("tap one to zoom!");
  } else {
    // 2x nearest-neighbour zoom of the chosen sticker
    const uint16_t *s = STK[stickerSel];
    float bob = sinf(t / 400.0f) * 5;
    int ox = 120, oy = 100 + (int)bob;
    for (int y = 0; y < 120; y++) {
      for (int x = 0; x < 120; x++) {
        uint16_t c = pgm_read_word(&s[y * 120 + x]);
        if (c != C_BG) {
          gfx->drawPixel(ox + 2*x,   oy + 2*y,   c); gfx->drawPixel(ox + 2*x+1, oy + 2*y,   c);
          gfx->drawPixel(ox + 2*x,   oy + 2*y+1, c); gfx->drawPixel(ox + 2*x+1, oy + 2*y+1, c);
        }
      }
    }
    gfx->drawRoundRect(112, 92 + (int)bob, 256, 256, 14, holoAt(0, t));
    holoText("this one? :3", 168, 380, 3, t);
    gfx->setTextSize(2); gfx->setTextColor(C_DIM);
    gfx->setCursor(158, 424); gfx->print("tap to go back");
  }
}

// --- live photo wall: people post pics from the PWA, they land here ---
uint16_t *photoFB = nullptr;                // 320x320 decode target
int photoW = 0, photoH = 0;
long long photoSeen = 0;                    // last photoTs shown (64-bit! epoch-ms overflows long)
bool firstSync = true;                      // suppress pop-ups on the boot-time sync
String pollDbg = "no poll yet";             // shown small on the MESSAGES screen
JPEGDEC jpegDec;
int JPEGDraw(JPEGDRAW *pDraw) {
  if (!photoFB) return 1;
  for (int yy = 0; yy < pDraw->iHeight; yy++) {
    int fy = pDraw->y + yy;
    if (fy < 0 || fy >= 320) continue;
    for (int xx = 0; xx < pDraw->iWidth; xx++) {
      int fx = pDraw->x + xx;
      if (fx >= 0 && fx < 320) photoFB[fy * 320 + fx] = pDraw->pPixels[yy * pDraw->iWidth + xx];
    }
  }
  return 1;
}
void fetchPhoto() {
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(6000);
  if (!http.begin(client, String("https://") + HUB_HOST + "/photo.jpg")) return;
  if (http.GET() == 200) {
    int len = http.getSize();
    if (len > 0 && len < 260000) {
      uint8_t *buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
      if (buf) {
        WiFiClient *s = http.getStreamPtr();
        int got = 0;
        while (got < len && http.connected()) {
          int r = s->readBytes(buf + got, len - got);
          if (r <= 0) break; got += r;
        }
        if (got == len) {
          if (!photoFB) photoFB = (uint16_t*)heap_caps_calloc(320 * 320, 2, MALLOC_CAP_SPIRAM);
          if (photoFB && jpegDec.openRAM(buf, len, JPEGDraw)) {
            jpegDec.setPixelType(RGB565_LITTLE_ENDIAN);
            memset(photoFB, 0, 320 * 320 * 2);
            photoW = jpegDec.getWidth(); photoH = jpegDec.getHeight();
            jpegDec.decode(0, 0, 0);
            jpegDec.close();
          }
        }
        free(buf);
      }
    }
  }
  http.end();
}

void scrFilm(uint32_t t) {
  header(photoFB ? "PHOTO WALL - LIVE" : "POLAROIDS", t);
  // filmstrip sprockets
  for (int y = 60; y < 460; y += 40) {
    gfx->fillRoundRect(6, y, 18, 24, 4, 0x2124);
    gfx->fillRoundRect(456, y, 18, 24, 4, 0x2124);
  }
  if (photoFB) {
    float bob = sinf(t / 700.0f) * 5;
    int px = 80, py = 66 + (int)bob;                            // 320x320 buffer centred
    gfx->fillRect(px - 12, py - 12, 344, 366, 0xFFFF);          // polaroid frame
    gfx->draw16bitRGBBitmap(px, py, photoFB, 320, 320);
    holoText("from the con floor!", 116, 420, 2, t);
  } else {
    float b1 = sinf(t / 700.0f) * 7, b2 = sinf(t / 700.0f + 2.1f) * 7;
    gfx->draw16bitRGBBitmap(38,  90 + (int)b1, (uint16_t*)pol1, POL1_W, POL1_H);
    gfx->draw16bitRGBBitmap(230, 140 + (int)b2, (uint16_t*)pol2, POL2_W, POL2_H);
    gfx->setTextSize(2); gfx->setTextColor(C_DIM);
    gfx->setCursor(90, 434); gfx->print("post a pic from the QR page!");
  }
}

// --- CINEMA (GIF -> persistent PSRAM framebuffer, blitted every render) ---
int gifX = 130, gifY = 120;
uint16_t *gifFB = nullptr;                  // 220x220 decode target
void GIFDraw(GIFDRAW *pDraw) {
  if (!gifFB) return;
  uint8_t *s = pDraw->pPixels;
  uint16_t *pal = pDraw->pPalette;
  int y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= 220) return;
  for (int x = 0; x < pDraw->iWidth; x++) {
    uint8_t idx = s[x];
    if (pDraw->ucHasTransparency && idx == pDraw->ucTransparent) continue;
    int fx = pDraw->iX + x;
    if (fx >= 0 && fx < 220) gifFB[y * 220 + fx] = pal[idx];
  }
}
void scrCinema(uint32_t t) {
  header("FEN TV", t);
  gfx->drawRoundRect(gifX - 10, gifY - 10, 240, 240, 12, holoAt(0, t));
  gfx->drawRoundRect(gifX - 12, gifY - 12, 244, 244, 14, holoAt(3, t));
  static uint32_t nextFrame = 0;
  if (gifOpen && t >= nextFrame) {
    int delayMs = 0;
    if (!gif.playFrame(false, &delayMs)) gif.reset();   // end of loop -> restart
    nextFrame = t + (delayMs > 0 ? delayMs : 200);      // honour the GIF's own timing
  }
  if (gifFB) gfx->draw16bitRGBBitmap(gifX, gifY, gifFB, 220, 220);
  holoText("now playing: pup.gif", 100, 390, 2, t);
}

void scrBoop(uint32_t t) {
  particles(t);
  header("BOOP COUNTER", t);
  gfx->setTextSize(3); gfx->setTextColor(C_DIM);
  gfx->setCursor(150, 90); gfx->print("total boops");
  char buf[16]; snprintf(buf, sizeof buf, "%ld", boopTotal < 0 ? 0 : boopTotal);
  int len = strlen(buf);
  int ts = (len > 4 ? 10 : 13) + (int)(burst * 3);
  gfx->setTextSize(ts);
  gfx->setCursor(240 - len * ts * 3, 170);
  gfx->setTextColor(burst > 0.05f ? C_PINK : C_GOLD); gfx->print(buf);
  // burst rings
  if (burst > 0.02f) {
    int r = (int)((1.0f - burst) * 320) + 30;
    gfx->drawCircle(240, 240, r, C_PINK);
    gfx->drawCircle(240, 240, r + 3, C_PURPLE);
    gfx->drawCircle(240, 240, (int)(r * 0.7f), C_TQ);
    burst *= 0.93f;
  }
  float pulse = 1.0f + sinf(t / 300.0f) * 0.08f;
  gfx->setTextSize((int)(4 * pulse) < 3 ? 3 : 4);
  gfx->setTextColor(C_PINK);
  gfx->setCursor(96, 340); gfx->print("TAP TO BOOP!");
}

void scrQR(uint32_t t) {
  header("SCAN TO BOOP ME", t);
  gfx->draw16bitRGBBitmap(90, 78, (uint16_t*)qr_img, QR_IMG_W, QR_IMG_H);
  gfx->drawRoundRect(84, 72, 312, 312, 10, holoAt(0, t));
  gfx->setTextSize(2); gfx->setTextColor(C_TQ);
  gfx->setCursor(84, 410); gfx->print("boops + live messages, live!");
}

void scrMsg(uint32_t t) {
  particles(t);
  header("LIVE MESSAGES", t);
  if (lastMsg.length() == 0) {
    gfx->setTextSize(3); gfx->setTextColor(C_DIM);
    gfx->setCursor(92, 220); gfx->print("no messages yet");
  } else {
    gfx->setTextSize(4); gfx->setTextColor(C_INK);
    int y = 120; String line = "";
    for (unsigned i = 0; i <= lastMsg.length(); i++) {
      if (i == lastMsg.length() || (lastMsg[i] == ' ' && line.length() > 16)) {
        if (i < lastMsg.length()) line += lastMsg[i];
        gfx->setCursor(40, y); gfx->print(line); y += 44; line = "";
        if (y > 340) break;
      } else line += lastMsg[i];
    }
    gfx->setTextSize(2); gfx->setTextColor(C_GOLD);
    gfx->setCursor(40, 396); gfx->print("- "); gfx->print(lastMsgFrom);
  }
  gfx->setTextSize(1); gfx->setTextColor(0x39E7);   // tiny sync debug line
  gfx->setCursor(16, 448); gfx->print(pollDbg);
}

// =====================================================================
void render() {
  uint32_t t = millis();
  gfx->fillScreen(C_BG);
  switch (screen) {
    case 0: scrHome(t); break;
    case 1: scrFursona(t); break;
    case 2: scrStickers(t); break;
    case 3: scrFilm(t); break;
    case 4: scrCinema(t); break;
    case 5: scrBoop(t); break;
    case 6: scrQR(t); break;
    case 7: scrMsg(t); break;
  }
  gfx->flush();   // (scanline overlay removed — read as artifacts on the real panel)
}

void wipeTo(int next) {
  // holo bar sweeps across as the screen changes
  for (int x = 0; x <= 480; x += 60) {
    for (int i = 0; i < 6; i++) gfx->fillRect(x - 60, i * 80, 60, 80, HOLO[i]);
    gfx->flush(); delay(12);
  }
  screen = next; stickerSel = -1;
  render();
}

// =====================================================================
void poll() {
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(3500);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/poll?device=" DEVICE_ID "&fw=" FW_VER)) {
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (!err) {
        long nb = doc["boops"] | -1L;
        if (nb != boopTotal) { if (boopTotal >= 0) burst = 1.0f; boopTotal = nb; }
        String nm = "", nf = "";
        if (!doc["msg"].isNull()) {
          nm = String((const char*)(doc["msg"]["text"] | ""));
          nf = String((const char*)(doc["msg"]["from"] | ""));
        }
        bool newMsg = (nm.length() && nm != lastMsg);
        lastMsg = nm; lastMsgFrom = nf;
        long long pts = doc["photoTs"].as<long long>();
        bool newPhoto = (pts > 0 && pts != photoSeen);
        if (newPhoto) { photoSeen = pts; fetchPhoto(); }
        pollDbg = String("ok b:") + nb + " m:" + nm.length() + " p:" + (long)(pts % 100000);
        // live pop-ups (not on the boot-time sync)
        if (!firstSync) {
          if (newPhoto && photoFB) wipeTo(3);
          else if (newMsg) wipeTo(7);
        }
        firstSync = false;
      } else pollDbg = String("json err: ") + err.c_str();
    } else pollDbg = String("http ") + code;
    http.end();
  } else pollDbg = "begin fail";
}

void sendBoop() {
  boopTotal++; burst = 1.0f;
  if (!wifiOk) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.setTimeout(3500);
  if (http.begin(client, String("https://") + HUB_HOST + "/api/boop")) {
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"device\":\"" DEVICE_ID "\",\"who\":\"badge tap\"}");
    http.end();
  }
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  WS_CH32_IO::begin(Wire, WS_CH32_IO::DEFAULT_I2C_SDA, WS_CH32_IO::DEFAULT_I2C_SCL,
                    WS_CH32_IO::DEFAULT_I2C_FREQ, &Serial);
  disp->begin();
  gfx->begin(GFX_SKIP_OUTPUT_BEGIN);
  gfx->setRotation(2);                       // lanyard orientation
  seedParts();

  touch.setPins(-1, -1);
  touchOk = touch.begin(Wire, 0x5D, 15, 7) || touch.begin(Wire, 0x14, 15, 7);

  gifFB = (uint16_t*)heap_caps_calloc(220 * 220, 2, MALLOC_CAP_SPIRAM);
  gif.begin(GIF_PALETTE_RGB565_LE);
  gifOpen = gif.open((uint8_t*)fen_gif, FEN_GIF_LEN, GIFDraw);

  // wifi setup screen (static)
  gfx->fillScreen(C_BG);
  header("WIFI SETUP", 0);
  gfx->setTextSize(3); gfx->setTextColor(C_INK);
  gfx->setCursor(40, 130); gfx->print("connect your phone to");
  gfx->setTextColor(C_TQ); gfx->setCursor(40, 180); gfx->print("WiFi: FEN-BADGE");
  gfx->setTextColor(C_INK); gfx->setCursor(40, 240); gfx->print("then pick the con WiFi");
  gfx->flush();

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wifiOk = wm.autoConnect("FEN-BADGE");
  if (wifiOk) poll();
}

void loop() {
  // touch
  if (touchOk) {
    static int16_t lastX = 0, lastY = 0;
    int16_t x[1], y[1];
    uint8_t n = touch.getPoint(x, y, 1);
    if (n > 0) {
      x[0] = 479 - x[0]; y[0] = 479 - y[0];
      if (!tDown) { tDown = true; tDownX = x[0]; tDownY = y[0]; }
      lastX = x[0]; lastY = y[0];
    } else if (tDown) {
      tDown = false;
      int dx = lastX - tDownX;
      if (abs(dx) > 70) {
        wipeTo((screen + (dx < 0 ? 1 : NSCREENS - 1)) % NSCREENS);
      } else {
        if (screen == 5) sendBoop();
        else if (screen == 2) {
          if (stickerSel >= 0) stickerSel = -1;
          else {
            // map tap to grid cell
            int cx = (lastX - 45) / 140, cy = (lastY - 80) / 160;
            if (cx >= 0 && cx < 3 && cy >= 0 && cy < 2) stickerSel = cy * 3 + cx;
          }
        }
      }
    }
  }

  if (millis() - lastPoll > 4000) { lastPoll = millis(); poll(); }
  render();                                   // continuous ~30 fps
}
