# Firmware — build & flash

Both firmwares are plain **Arduino_GFX** (no LVGL — fewer bring-up risks), pinned to the
library versions Waveshare ships with their own demos:
- esp32 core **3.0.7** (Waveshare-recommended)
- GFX_Library_for_Arduino **1.5.3** (copied from the Waveshare LCD-4 repo, with two
  unused bus files guarded for IDF 5.1 — see note below)
- WS_CH32_IO + SensorLib from the Waveshare repo, WiFiManager, ArduinoJson

## 4" badge — `firmware/s3-lcd4/`
Board: ESP32-S3-Touch-LCD-4 (V4.0, CH32V003 io-expander @ I2C 0x24, ST7701 RGB panel,
GT911 touch). Pin map is from Waveshare's official `01_HelloWorld` demo.

```bash
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,CPUFreq=240"
arduino-cli compile -b "$FQBN" firmware/s3-lcd4
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodem* firmware/s3-lcd4
```
Controls: **swipe** left/right = screens · **tap** on the BOOP screen = boop.

## 1.47" pendant — `firmware/c6-lcd147/`
Board: ESP32-C6-LCD-1.47 (ST7789 172×320, offsets 34/0, WS2812 on GPIO8, BOOT btn GPIO9).

```bash
FQBN="esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=huge_app"
arduino-cli compile -b "$FQBN" firmware/c6-lcd147
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodem* firmware/c6-lcd147
```
Controls: **short press** BOOT = next screen · **hold (>0.6 s)** = boop (LED flashes).

Pre-built binaries are in `firmware/*/build/` (`.merged.bin` = flash at 0x0 with esptool).

## First boot / WiFi
1. Badge shows **WIFI SETUP** → join WiFi `FEN-BADGE` (4") / `FEN-PENDANT` (C6) from a phone
2. Captive portal opens → pick the con/hotel WiFi + password
3. Credentials persist; next boots skip straight to the badge. Portal times out after
   3 min → badge continues fully offline (all screens work, no live sync).

## Assets
`firmware/shared/png2c.py` converts PNGs → RGB565 headers (fen art + hub QR).
If the hub URL changes: regenerate `qr-hub.png` (api.qrserver.com), rerun
`python3 png2c.py`, copy the headers into both sketch dirs, rebuild.

## Notes / gotchas
- **GFX 1.5.3 + core 3.0.7:** `Arduino_ESP32QSPI.cpp` / `Arduino_ESP32LCD8.cpp` need
  IDF ≥5.2; both are unused here and wrapped in an IDF version guard (marker
  `FEN_IDF_GUARD`) inside `~/Documents/Arduino/libraries/GFX_Library_for_Arduino`.
- **C6 uses `Arduino_HWSPI`** (not `Arduino_ESP32SPI`, which is compiled out for C6 in 1.5.3).
- On the 4", `WS_CH32_IO::begin()` **must** run before `gfx->begin()` — the expander
  powers/resets the panel. Skipping it = black screen.
- HTTPS uses `setInsecure()` (no cert pinning) — fine for a boop counter.
