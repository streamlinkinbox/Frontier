# Charge backend v1 — Xsolla webhook receiver + entitlements

One Python file, stdlib only, SQLite storage. It does three jobs:

1. Catches Xsolla webhooks (`order_paid`, `order_canceled`, `user_validation`).
2. Hands out premium + AutoCoin, idempotent per order, exact revoke on cancel.
3. Answers the game client's "is this Epic account premium?" question.

## Run it

```sh
cd Backend
XSOLLA_SECRET_KEY=paste-from-publisher-account \
CHARGE_API_KEY=pick-a-game-password \
python3 webhook_receiver.py
# listening on 0.0.0.0:8080
```

Env vars: `XSOLLA_SECRET_KEY` (required for real webhooks),
`XSOLLA_REQUIRE_SIGNATURE=0` (local signature-less tests only, never live),
`CHARGE_API_KEY` (optional shared password between game and backend),
`PORT` (default 8080), `DB_PATH` (default `charge.db`).

Tests (run from repo root):

```sh
python3 -m unittest discover -s Backend -p 'test_*.py' -v
```

## Xsolla dashboard setup

1. Project Charge → Settings → Webhooks: add
   `https://YOUR-HOST/webhooks/xsolla` (HTTPS required by Xsolla).
2. Enable notifications: `user_validation`, `order_paid` (`Successful payment
   for order`), `order_canceled` (`Order cancellation`).
3. Copy the webhook secret key into `XSOLLA_SECRET_KEY` on the host.
4. In the game (later): pass the player's Epic account ID as
   `user.id.value` when creating the payment token, so grants land on the
   right account.

## Endpoints

- `GET /healthz` → `{"ok": true}`.
- `POST /v1/register` `{"epic_account_id": "…", "display_name": "…"}` →
  upserts the player, returns premium state.
- `GET /v1/premium?epic_account_id=…` → `{"premium": true/false,
  "source": "lifetime|…|none", "autocoin": N}`. This feeds the game's
  `ResolvePremiumAccess()`.
- `POST /webhooks/xsolla` — Xsolla only. Verifies the
  `Authorization: Signature <sha1(raw_body + secret)>` header, then grants.
  Always answers 204 after handling (even unknown SKUs) so Xsolla never
  retry-storms; 500 only on real failures so Xsolla DOES retry those.

`/v1/*` requires header `X-Charge-Key` when `CHARGE_API_KEY` is set.

## Game integration (Charge.backend.ini)

At transport start the game reads `Charge.backend.ini` from the same
folders as `Charge.local.ini` and calls `GET /v1/premium`:

```ini
version=1
backend_url=http://127.0.0.1:8080
backend_key=<same value as CHARGE_API_KEY>
```

`premium=true` routes to Photon; anything missing or failing falls back
to the manual Setup toggle and login never breaks. Watch the redacted
log for `backend=ok host=... premium=1 coins=N`.

Local test: run this server on your PC, point the ini at it, log in,
then fire the Publisher Account webhook tester (or a real sandbox
purchase) and confirm the next login reports premium.

## Hosting notes

- Needs a public HTTPS address Xsolla can reach 24/7 (any cheap VPS with
  Caddy/nginx, or a Python-friendly host). This repo does not host it.
- Back up `charge.db` — it IS the ledger. Later: move to Postgres.
- Never put `XSOLLA_SECRET_KEY` or `CHARGE_API_KEY` in the repo, the game
  build, or chat. Env vars on the host only.

## Roadmap

- Strict `user_validation` (reject unknown ids) once the game registers
  logins via `/v1/register`.
- Subscription status (`premium_monthly/yearly`) from Xsolla subscription
  webhooks.
- Server-side race entry fees, exchange, and reward payouts (the arcade
  pricing in `Docs/XsollaStorePlan.md`).
- Photon Custom Authentication verdicts from the premium table.
