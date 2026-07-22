// =====================================================================
//  fen.dog device hub — Fastify + WebSocket + SQLite
//  Serves: REST API for devices/boops/messages, WS live push,
//          and the phone PWA (static from ../public).
//
//  Devices (ESP32 badges) connect via  WS /ws?device=<id>&key=<key>
//  Phones use the PWA which talks REST + WS /ws?client=phone
// =====================================================================
import Fastify from 'fastify';
import websocket from '@fastify/websocket';
import fastifyStatic from '@fastify/static';
import cors from '@fastify/cors';
import Database from 'better-sqlite3';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { randomBytes } from 'node:crypto';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DATA_DIR = process.env.DATA_DIR || join(__dirname, '..', 'data');
const PORT = Number(process.env.PORT || 3000);
const ADMIN_KEY = process.env.ADMIN_KEY || 'fen-awoo-2026';

// ---------- db ----------
import { mkdirSync } from 'node:fs';
mkdirSync(DATA_DIR, { recursive: true });
const db = new Database(join(DATA_DIR, 'fen-devices.sqlite'));
db.pragma('journal_mode = WAL');
db.exec(`
  CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,           -- e.g. "lcd4" | "c6" | custom
    name TEXT NOT NULL,
    key TEXT NOT NULL,             -- device auth key
    last_seen INTEGER,
    fw TEXT,
    created INTEGER NOT NULL
  );
  CREATE TABLE IF NOT EXISTS boops (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,
    who TEXT DEFAULT '',           -- optional name/emoji from the booper
    ts INTEGER NOT NULL
  );
  CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,                -- target device or NULL = all
    text TEXT NOT NULL,
    from_name TEXT DEFAULT 'Fen',
    ts INTEGER NOT NULL,
    shown INTEGER DEFAULT 0
  );
`);

const q = {
  upsertDevice: db.prepare(`INSERT INTO devices (id,name,key,created,last_seen) VALUES (?,?,?,?,?)
    ON CONFLICT(id) DO UPDATE SET last_seen=excluded.last_seen`),
  touchDevice: db.prepare(`UPDATE devices SET last_seen=?, fw=COALESCE(?,fw) WHERE id=?`),
  getDevice: db.prepare(`SELECT * FROM devices WHERE id=?`),
  listDevices: db.prepare(`SELECT id,name,last_seen,fw FROM devices`),
  addBoop: db.prepare(`INSERT INTO boops (device_id,who,ts) VALUES (?,?,?)`),
  boopCount: db.prepare(`SELECT COUNT(*) c FROM boops`),
  boopCountDevice: db.prepare(`SELECT COUNT(*) c FROM boops WHERE device_id=?`),
  recentBoops: db.prepare(`SELECT who,ts,device_id FROM boops ORDER BY id DESC LIMIT ?`),
  addMsg: db.prepare(`INSERT INTO messages (device_id,text,from_name,ts) VALUES (?,?,?,?)`),
  recentMsgs: db.prepare(`SELECT * FROM messages ORDER BY id DESC LIMIT ?`),
};

// seed the two known devices with stable keys (idempotent)
const now = Date.now();
for (const [id, name] of [['lcd4', 'Fen Badge 4"'], ['c6', 'Fen Pendant 1.47"']]) {
  if (!q.getDevice.get(id)) q.upsertDevice.run(id, name, randomBytes(8).toString('hex'), now, null);
}

// ---------- app ----------
const app = Fastify({ logger: true });
await app.register(cors, { origin: true });
await app.register(websocket);
await app.register(fastifyStatic, { root: join(__dirname, '..', 'public') });

// live connections
const deviceSockets = new Map();   // device_id -> Set<ws>
const phoneSockets = new Set();

function pushToDevice(id, obj) {
  const set = deviceSockets.get(id);
  if (set) for (const ws of set) { try { ws.send(JSON.stringify(obj)); } catch {} }
}
function pushToAllDevices(obj) { for (const id of deviceSockets.keys()) pushToDevice(id, obj); }
function pushToPhones(obj) {
  for (const ws of phoneSockets) { try { ws.send(JSON.stringify(obj)); } catch {} }
}
function stats() {
  return {
    type: 'stats',
    boops: q.boopCount.get().c,
    devices: q.listDevices.all().map(d => ({
      ...d, online: deviceSockets.has(d.id) && deviceSockets.get(d.id).size > 0,
      boops: q.boopCountDevice.get(d.id).c,
    })),
  };
}

// ---------- REST ----------
app.get('/api/health', async () => ({ ok: true, ts: Date.now() }));
app.get('/api/stats', async () => stats());
app.get('/api/boops', async (req) => ({
  total: q.boopCount.get().c,
  recent: q.recentBoops.all(Number(req.query.limit || 20)),
}));

// public boop — anyone scanning the QR can boop
app.post('/api/boop', async (req) => {
  const { device = null, who = '' } = req.body || {};
  q.addBoop.run(device, String(who).slice(0, 40), Date.now());
  const payload = { type: 'boop', total: q.boopCount.get().c, who: String(who).slice(0, 40), device };
  device ? pushToDevice(device, payload) : pushToAllDevices(payload);
  pushToPhones(payload);
  return { ok: true, total: payload.total };
});

// send a message to a device screen (from the PWA)
app.post('/api/message', async (req, reply) => {
  const { device = null, text, from = 'Fen', key } = req.body || {};
  if (key !== ADMIN_KEY) return reply.code(401).send({ error: 'bad key' });
  if (!text) return reply.code(400).send({ error: 'text required' });
  q.addMsg.run(device, String(text).slice(0, 200), String(from).slice(0, 40), Date.now());
  const payload = { type: 'message', text: String(text).slice(0, 200), from: String(from).slice(0, 40), device };
  device ? pushToDevice(device, payload) : pushToAllDevices(payload);
  pushToPhones(payload);
  return { ok: true };
});

app.get('/api/messages', async (req) => q.recentMsgs.all(Number(req.query.limit || 20)));

// ---------- WebSocket ----------
app.get('/ws', { websocket: true }, (socket, req) => {
  const url = new URL(req.url, 'http://x');
  const deviceId = url.searchParams.get('device');
  const isPhone = url.searchParams.get('client') === 'phone';

  if (deviceId) {
    if (!deviceSockets.has(deviceId)) deviceSockets.set(deviceId, new Set());
    deviceSockets.get(deviceId).add(socket);
    q.touchDevice.run(Date.now(), url.searchParams.get('fw'), deviceId);
    app.log.info({ deviceId }, 'device connected');
    socket.send(JSON.stringify({ type: 'hello', ...statsForDevice(deviceId) }));
    pushToPhones(stats());
    socket.on('message', (raw) => {
      try {
        const m = JSON.parse(raw);
        if (m.type === 'boop') {           // physical boop on the device touchscreen
          q.addBoop.run(deviceId, m.who || '', Date.now());
          const payload = { type: 'boop', total: q.boopCount.get().c, who: m.who || '', device: deviceId };
          pushToAllDevices(payload); pushToPhones(payload);
        }
        if (m.type === 'ping') { q.touchDevice.run(Date.now(), null, deviceId); socket.send('{"type":"pong"}'); }
      } catch {}
    });
    socket.on('close', () => {
      deviceSockets.get(deviceId)?.delete(socket);
      pushToPhones(stats());
    });
  } else if (isPhone) {
    phoneSockets.add(socket);
    socket.send(JSON.stringify(stats()));
    socket.on('close', () => phoneSockets.delete(socket));
  } else {
    socket.close();
  }
});

function statsForDevice(id) {
  return { boops: q.boopCount.get().c, deviceBoops: q.boopCountDevice.get(id).c };
}

// SPA-ish fallback → PWA
app.setNotFoundHandler((req, reply) => {
  if (req.raw.url?.startsWith('/api') || req.raw.url?.startsWith('/ws')) return reply.code(404).send({ error: 'not found' });
  return reply.sendFile('index.html');
});

await app.listen({ port: PORT, host: '0.0.0.0' });
console.log(`fen device hub on :${PORT}`);
