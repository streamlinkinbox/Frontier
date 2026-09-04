# Frontier — Lobby + Wallet HMI (HTML mock)

Tablet-style HTML mock of the **Lobby** and **Wallet** experience, in the
FRONTIER blue · black · white design system (Inter). Zero backend — all data
is deterministic mock, EOS flows are labelled inline (`EOS_Lobby_*`,
`EOS_RTC`, `EOS_Ecom_*`, …) so the real SDK calls have a home later.

## Run it

Serve the `app/` folder with any static server, e.g.:

```bash
cd app && python3 -m http.server 8099 --bind 0.0.0.0
# open http://localhost:8099  → Home launcher (app grid)
```

## What's inside

- `app/index.html` — **Home launcher**: Android-style app grid + dock that
  opens the installed apps (Lobby, Wallet). Leaderboard + Garage slots are
  reserved as "SOON". Live clock, app search filter.
- `app/lobby.html` — **Lobby**: browse/search/filter 18 mock lobbies → join a
  room → roster + voice + chat sim, boarding-pass ticket with pit-box picker,
  ready-up → countdown → session live. Create-lobby modal included.
  Standalone app — nav is Home / Lobby / Leaderboard only.
- `app/wallet.html` — **Wallet** (standalone, built from the crypto/aurora
  reference screens — no lobby links anywhere): aurora balance card with live
  count-up, **Send / + / Receive** action bar (gift / top-up / redeem),
  CR + RP holdings with weekly movers, **Assets** (owned + equip) / **Market**
  (All · ★ · Top movers · Top rated · 7 categories with ▲▼ deltas) / **History**
  tabs, item detail + buy flows with shortfall → top-up, earn list.
  Persists to `localStorage` (`frontier.wallet.v2`).
- `app/fonts/` — Inter OFL license. **The `.woff2` binary is git-ignored by
  the sandbox network** — restore it with (needs internet):

```bash
curl -sSL -o app/fonts/inter-latin-wght-normal.woff2 \
  "https://cdn.jsdelivr.net/fontsource/fonts/inter:vf@latest/latin-wght-normal.woff2"
```

Until then the UI falls back to system fonts automatically.

## Still to build

- **Leaderboard** tab (currently a "coming next" stub in both pages' nav)
- Home tab, spectator mode, real EOS + backend wiring

## History note

Branch history also contains an earlier 3D-cockpit HMI prototype
(`56f4bed`, `42efe30`) — superseded by this base, recoverable via git.
