# Frontier — In-Vehicle HMI Concept

3D car-interior preview of the **Lobby + Leaderboard** experience, built as one
HTML application. The UI lives on a dashboard tablet rendered *inside* a
Three.js night-drive cockpit as real, clickable HTML (CSS3D) — fully interactive.

## Run it

Any static server works, e.g.:

```bash
python3 -m http.server 8099 --bind 0.0.0.0
# open http://localhost:8099
```

## What's inside

- `index.html` — stage, HUD, and the tablet-screen HMI DOM (Lobby + Leaderboard tabs)
- `styles.css` — neon HMI theme + HUD
- `mock-eos.js` — **Mock EOS layer**: fake lobby/presence/chat/leaderboard sync
  (`EOS_Lobby_*`, `EOS_Presence_*`, `EOS_Leaderboards_*` flows, all client-side)
- `main.js` — Three.js cockpit (dashboard, wheel, seats, LED strips, moving night
  road) + all HMI wiring (roster, ready-up, host controls, vehicle select, squad
  chat, countdown launch, live leaderboard, EOS event feed)

## Controls

- Drag to orbit, scroll to zoom, **click the tablet** to focus it, `Esc` to go back
- Camera presets: Cockpit / Tablet / Driver / Road (top bar)
- Lobby SIM buttons (`+ Join`, `− Leave`, `⇄ Host`) drive the mock multiplayer feed

> Mock build: zero backend, zero network. Swap `mock-eos.js` calls for the real
> EOS SDK when wiring the actual game.
