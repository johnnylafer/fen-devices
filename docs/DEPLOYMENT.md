# Deployment — Coolify on Tonkai

The hub runs on the Tonkai Coolify server (188.166.49.216), deployed via the
Coolify v4 REST API from this repo's `server/Dockerfile`.

## Live setup
| thing | value |
|---|---|
| URL | `https://fen.188.166.49.216.sslip.io` (sslip.io → resolves to the server, real Let's Encrypt cert, no DNS setup) |
| Coolify project | `fen-devices` (uuid `g132brf0gyk7u1vjrbd109e1`) |
| Application | `fen-hub` (uuid `pkgo9scleibobcjl0pobov9f`) |
| Source | github.com/johnnylafer/fen-devices, branch `main`, base dir `/server`, Dockerfile build |
| Port | 3000 (internal; Traefik terminates HTTPS + proxies WS automatically) |
| Volume | `fen-hub-data` → `/data` (SQLite lives here, survives redeploys) |
| Env | `ADMIN_KEY` (secret), `DATA_DIR=/data` |

## Redeploy after a code change
```bash
git push   # then:
curl -s -H "Authorization: Bearer <COOLIFY_TOKEN>" \
  -X POST "https://coolify.tonkai.xyz/api/v1/deploy?uuid=pkgo9scleibobcjl0pobov9f"
```
Deploys take ~60 s. Status:
```bash
curl -s -H "Authorization: Bearer <COOLIFY_TOKEN>" \
  https://coolify.tonkai.xyz/api/v1/deployments/applications/pkgo9scleibobcjl0pobov9f
```

## Attach a real domain later (e.g. awoo.tonkai.xyz)
`*.tonkai.xyz` already points at this server, so:
```bash
curl -s -H "Authorization: Bearer <COOLIFY_TOKEN>" -H "Content-Type: application/json" \
  -X PATCH https://coolify.tonkai.xyz/api/v1/applications/pkgo9scleibobcjl0pobov9f \
  -d '{"domains":"https://awoo.tonkai.xyz"}'
# then redeploy (command above). Cert auto-issues.
```
⚠️ If the domain changes, re-generate the QR assets (`firmware/shared/`) and reflash,
since the badge QRs embed the URL.

## API surface
| endpoint | what |
|---|---|
| `GET /api/health` | liveness |
| `GET /api/stats` | totals + per-device presence |
| `POST /api/boop {device?,who?}` | public boop |
| `GET /api/poll?device=id&fw=v` | device heartbeat + totals + latest message |
| `POST /api/message {text,device?,from?,key}` | push to badge screens (needs ADMIN_KEY) |
| `GET /ws?client=phone` / `GET /ws?device=id` | live WebSocket |
