# ☀️ Morning checklist — what to look at

## 1. The 4" badge (it's flashed and was left plugged in)
Look at the screen. You should see either:
- **"WIFI SETUP / connect your phone to WiFi: FEN-BADGE"** → firmware is alive ✅
  Join `FEN-BADGE` from your phone, pick your home WiFi in the portal, and it flips
  to the badge. Swipe through the 5 screens; tap the BOOP screen → your phone PWA
  counter should tick within ~4 s.
- **The badge screen directly** (if the portal timed out overnight) → also fine;
  press RESET to redo WiFi.
- **Black screen** → tell Claude "4-inch is black" — most likely panel-init tuning,
  fix + reflash takes minutes.

## 2. The PWA on your phone
Open **https://fen.188.166.49.216.sslip.io** → Share → *Add to Home Screen*.
- Big BOOP button (live counter), device presence, message sender.
- Admin key for sending messages: see `Awoostria/fen-hub-secrets.md`.

## 3. The C6 pendant
Not flashed (wasn't plugged in). Plug it in and either say "flash the pendant" or:
```bash
cd "/Users/johnny/Private Projects/Awoostria/fen-devices"
arduino-cli upload -b "esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=huge_app" \
  -p /dev/cu.usbmodem* firmware/c6-lcd147
```
Its WiFi AP is `FEN-PENDANT`; BOOT short-press = next screen, hold = boop.

## 4. Con-WiFi reality check
Hotel/con WiFi with a browser login page (not just a password) can block the badges.
Fallback that always works: **iPhone hotspot** → join both badges to it once; they
remember it.

## Known open items
- Screen rendering verified by compile + official pin maps, **not by eyes** — cosmetic
  offsets (rotation, colors) may need one tweak round.
- QR on the badges points at the sslip.io URL. If you want `awoo.tonkai.xyz` instead:
  one API call + QR regen + reflash (see DEPLOYMENT.md).
