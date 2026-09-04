# Frontier — Lobby + Wallet HMI (HTML mock)

Tablet-style HTML mock of the **Lobby** and **Wallet** experience, in the
FRONTIER blue · black · white design system (Inter). Zero backend — all data
is deterministic mock, EOS flows are labelled inline (`EOS_Lobby_*`,
`EOS_RTC`, `EOS_Ecom_*`, …) so the real SDK calls have a home later.

## Run it

Serve the `app/` folder with any static server, e.g.:

```bash
cd app && python3 -m http.server 8099 --bind 0.0.0.0
# open http://localhost:8099  → Lobby
#      http://localhost:8099/wallet.html → Wallet
```

## What's inside

- `app/index.html` — **Lobby**: browse/search/filter 18 mock lobbies → join a
  room → roster + voice + chat sim, boarding-pass ticket with pit-box picker,
  ready-up → countdown → session live. Create-lobby modal included.
  Standalone app — nav is Home / Lobby / Leaderboard only.
- `app/wallet.html` — **Wallet** (standalone 2nd app): aurora balance card
  with quick Top-up pill, CR + RP portfolio tiles with weekly movers, top-up /
  gift / redeem / earn modals, store (decals · skins · wheels · audio ·
  lights · engine sounds · premium Levels) with trending ▲ / falling ▼ market
  deltas, categories, sort, wishlist, equip, buy flows, transaction history.
  Persists to `localStorage`.
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
