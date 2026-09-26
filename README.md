# DDAY — Breakwater

A low-poly D-Day landing level built with Three.js. You are trapped between a
**rising ocean tide** at your back and a **giant concrete wall** ahead. Reach
the gate alive.

## The level

- **Giant wall** — the objective. Heavy gate tunnel in the middle, floodlit
  parapet with lookout towers and merlons. Reach the gate to win.
- **Sentries** — soldiers in defensive concrete bunkers (pillboxes with firing
  slits) and on wall lookout towers. They open up on **vehicles** at long
  range and on infantry that gets close.
- **Low-poly car** — a lofted sedan (Sentra-style silhouette, not a box).
  Drive it at your own risk: it draws every gun on the wall.
- **Static tanks** — intact and wrecked low-poly tanks, never moving.
- **Mines** — flat tank-mine belts and anti-personnel mines that **explode**
  on contact.
- **Barricades** — Czech hedgehogs, dragon's teeth, tilted shore stakes,
  sandbag road blocks.
- **Defenses** — zigzag **trenches** carved into the terrain, huge **earth
  mounds**, **sandbag** walls, **barbed wire** entanglements.
- **Roads / pathways** — a main road to the gate plus side paths and a
  lateral track.
- **Rising tide** — the ocean slowly climbs the beach over 6 minutes,
  flooding the low ground (and your car) behind you.

## Controls

| Input | Action |
| --- | --- |
| WASD / arrows | move / drive |
| Mouse | look |
| Shift | sprint |
| Space | jump |
| E | enter / exit the car |
| Esc | release the mouse |

## Run

```bash
npm install
npm run dev      # http://localhost:5173
```

## Smoke tests

```bash
node scripts/smoke.mjs
```

Builds the whole scene headlessly and simulates walking, driving, mine
detonations, sentry fire, the tide, and the win condition.

## Structure

```
src/
  layout.js     level data (mounds, trenches, roads, minefields, defenses)
  terrain.js    height field + clean vertex-colored ground mesh
  ocean.js      low-poly water with the rising tide
  props.js      sandbags, hedgehogs, wire, stakes, dragon's teeth, mines
  vehicles.js   the lofted sedan + static tanks
  fortress.js   the great wall, bunkers, towers, sentry figures
  game.js       player/car controllers, sentry AI, explosions, tide, rules
  hud.js        DOM HUD
  main.js       renderer, sky, lights, loop
```
