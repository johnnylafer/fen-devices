# fen-devices 🐾

Fen's connected con-badge system for **Awoostria 2026** — two ESP32 badges, a live
web hub, and a phone PWA, all in fen.dog styling.

```
                    ┌──────────────────────────────┐
                    │   fen-hub  (Coolify/Tonkai)  │
                    │ https://fen.188.166.49.216   │
                    │        .sslip.io             │
                    │  Fastify · WS · SQLite       │
                    └──────┬───────────────┬───────┘
              HTTPS poll / │               │  WSS live
              POST boop    │               │
        ┌──────────────────┴───┐       ┌───┴──────────────┐
        │  ESP32 badges        │       │  Phone PWA       │
        │  · S3-Touch-LCD-4    │       │  boop button     │
        │  · C6-LCD-1.47       │       │  send messages   │
        └──────────────────────┘       │  device presence │
                                       └──────────────────┘
```

## What it does
- **Badges** show swipeable screens: BADGE · FURSONA · BOOP COUNTER · QR · LIVE MESSAGES
- Anyone **scans the QR** on a badge → lands on the PWA → taps **BOOP** → the counter
  updates on every badge and phone within seconds
- Fen (with the admin key) can **push messages** that appear on the badge screens live

## Layout
| path | what |
|---|---|
| `server/` | Fastify hub + PWA (`public/`) + Dockerfile |
| `firmware/s3-lcd4/` | 4" badge (480×480 touch) — swipe + tap-to-boop |
| `firmware/c6-lcd147/` | 1.47" pendant — BOOT btn: short=next, long=boop |
| `firmware/shared/` | png2c.py asset pipeline + generated RGB565 headers |
| `docs/` | deployment, firmware, morning checklist |

## Quick links
- Live hub + PWA: **https://fen.188.166.49.216.sslip.io**
- Deployed on Coolify (project `fen-devices`, app `fen-hub`): https://coolify.tonkai.xyz
- Docs: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) · [docs/FIRMWARE.md](docs/FIRMWARE.md) ·
  [docs/MORNING-CHECKLIST.md](docs/MORNING-CHECKLIST.md)

The admin key is **not** in this repo — it lives in `Awoostria/fen-hub-secrets.md` locally
and as the `ADMIN_KEY` env var on Coolify.
