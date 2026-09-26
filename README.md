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
  Drive it at your own risk: it draws every gun on the wall. A wrecked
  civilian sedan sits abandoned off the main road.
- **Static vehicles** — intact & wrecked low-poly tanks and canvas-covered
  military trucks, never moving. Wrecks smolder with smoke and embers.
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
- **Battlefield atmosphere** — drifting low-poly clouds, shell craters,
  smoke columns over the wrecks.

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
npm run smoke
```

Builds the whole scene headlessly and simulates walking, driving, mine
detonations, sentry fire, the tide, and the win condition.

Screenshot QA (needs the dev server running):

```bash
LD_LIBRARY_PATH=… node scripts/screenshot.mjs   # watermarked frames + state log
node scripts/pnginfo.mjs v9-2-spawn.png         # coarse pixel map
node scripts/scanline.mjs car-studio-side.png   # RGB scanlines
```

## Structure

```
src/
  layout.js     level data (mounds, trenches, roads, minefields, defenses)
  terrain.js    height field + clean vertex-colored ground mesh
  ocean.js      low-poly water with the rising tide
  props.js      sandbags, hedgehogs, wire, stakes, dragon's teeth, mines
  vehicles.js   the lofted sedan, static tanks, canvas-covered trucks
  fortress.js   the great wall, bunkers, towers, sentry figures
  atmos.js      wreck smoke, drifting clouds, shell craters
  game.js       player/car controllers, sentry AI, explosions, tide, rules
  hud.js        DOM HUD
  main.js       renderer, sky, lights, loop
```
