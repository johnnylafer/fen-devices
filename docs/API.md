# fen-hub API — integration reference for fen.dog / fen.bio

Base URL: **`https://fen.188.166.49.216.sslip.io`**
(may later move to `awoo.tonkai.xyz` or a fen.dog subdomain — everything below is path-relative)

CORS: **open** (`origin: true`) — call it straight from the fen.dog frontend.
Auth: **none** — boops, messages, and photos are intentionally public. No API keys in frontend code.
Live badge hardware polls this hub every ~4 s; anything you POST shows up on Fen's physical con badge.

## REST

### `GET /api/health`
`{ "ok": true, "ts": 1784793865900 }`

### `GET /api/stats`
```json
{ "type":"stats", "boops": 11, "devices":[
  { "id":"lcd4", "name":"Fen Badge 4\"", "last_seen":1784793865900,
    "fw":"2.0-holo", "online":true, "boops":4 } ] }
```
`online` = device WS-connected or HTTP-polled within 15 s.

### `POST /api/boop`  — the main event 🐾
Body: `{ "device": "lcd4" | null, "who": "name/emoji (optional, ≤40)" }`
→ `{ "ok":true, "total": 12 }`
`device:null` boops all badges. Triggers the burst animation on the badge + live WS event.

### `GET /api/boops?limit=20`
`{ "total":12, "recent":[ { "who":"...", "ts":..., "device_id":"lcd4" } ] }`

### `POST /api/message`
Body: `{ "text":"≤200 chars", "from":"≤40 (default Fen)", "device": null }`
→ `{ "ok":true }` · Badge auto-switches to its MESSAGES screen. Only the **latest** message is shown on-device.

### `GET /api/messages?limit=20` — message history (newest first)

### `POST /api/photo`
Body: `{ "jpeg": "<base64 or data:image/jpeg;base64,...>", "who":"optional" }`
Constraints: **JPEG only** (magic-byte checked), ≤250 KB decoded, ≤300 KB body.
**Client-side downscale first** (canvas → ≤320 px → `toDataURL('image/jpeg', 0.72)`).
→ `{ "ok":true, "ts":1784793865900 }` · Badge fetches it and pops its PHOTO WALL screen.
Single-slot: newest photo replaces the previous one.

### `GET /photo.jpg` — the current photo (404 if none yet). Cache-bust with `?ts=`.

### `GET /api/poll?device=<id>&fw=<ver>` — used by badges; also handy as a cheap "everything" read:
`{ "boops":12, "deviceBoops":4, "msg":{ "text","from","ts" }|null, "photoTs":1784793865900 }`
⚠️ `photoTs`/`ts` are epoch-ms — **do not parse into 32-bit ints**.

## WebSocket (live push)

`wss://<base>/ws?client=phone` — subscribe as a viewer. Server pushes JSON:
| event | payload |
|---|---|
| `stats` | full stats object (on connect + presence changes) |
| `boop` | `{ type:"boop", total, who, device }` |
| `message` | `{ type:"message", text, from, device }` |
| `photo` | `{ type:"photo", ts, who }` → refetch `/photo.jpg?ts` |

Reconnect on close (the PWA uses a 2.5 s retry). WS upgrades pass through Traefik untouched.

## Integration ideas for fen.dog/awoo
- Live boop counter on the page (WS `boop` events) — same number as the physical badge
- "Boop Fen" button → `POST /api/boop` with the visitor's name
- Message wall → `GET /api/messages` + WS, send box → `POST /api/message`
- Latest con photo → `/photo.jpg` + WS `photo` refresh
- Badge online indicator → `stats.devices[].online` ("Fen's badge is live at the con ✦")

## Abuse note
Everything is public by design (con-friendly). If it gets spammed: rate-limit at the hub or
re-gate messages/photos — one env var + redeploy (see DEPLOYMENT.md; ADMIN_KEY plumbing still exists server-side).

## Reference PWA
The hub serves its own PWA at `/` (`server/public/index.html` in github.com/johnnylafer/fen-devices) —
a working example of every call above, in fen.dog styling.
