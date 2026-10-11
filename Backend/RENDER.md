# Host the backend on Render (free tier, testing only)

## Deploy

1. Make sure this branch is pushed (it is — `render.yaml` is at repo root).
2. Sign up at render.com (GitHub login) → Dashboard → **New** → **Blueprint**.
3. Pick the `Frontier` repo. If it asks for a branch, choose
   `arena/9928a45d-frontier` (merge to `main` later for a permanent setup).
4. Name it `charge-backend`, plan **Free**. Render asks for two secrets:
   - `XSOLLA_SECRET_KEY`: Publisher Account → Charge → Settings →
     Webhooks → secret key.
   - `CHARGE_API_KEY`: make up a long random password. The SAME value
     goes into `Charge.backend.ini` on your PC later.
5. **Deploy**. Wait until it says Live. Your URL looks like
   `https://charge-backend-xxxx.onrender.com`.
6. Open `https://<your-url>/healthz` — you should see `{"ok": true}`.

## Connect Xsolla

Publisher Account → Charge → Settings → Webhooks:

1. Webhook URL: `https://<your-url>/webhooks/xsolla`.
2. Enable `user_validation`, `order_paid`, `order_canceled`.
3. Hit **Test** on each, then check the Render log tab — you should see
   the accept lines. Test purchases from `Tools/XsollaSandboxToken.ps1`
   land in the database within seconds.

## Free-tier caveats (read before demoing)

- **Sleeps after ~15 min idle.** First request takes ~1 min to wake it.
  Always open `/healthz` first, THEN pay — or Xsolla's validation call
  may time out and the test payment fails.
- **Disk is wiped on restart.** `charge.db` resets; grants vanish. Fine
  for testing the flow, never for real money.
- **Logs** live in the Render dashboard (Logs tab), not in files.
- Going live later = paid disk or Postgres + a host that doesn't sleep
  (cheap VPS or Render Starter). The code needs no changes for that.
