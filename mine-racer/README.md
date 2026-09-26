# GOLDRUSH GAUNTLET — 3D Mine-Shaft Maze Racer

A fully 3D (Three.js / WebGL) prototype of a maze-like mine you race a box car through,
with mine carts running on recessed rails as moving traffic hazards.

![genre](https://img.shields.io/badge/genre-arcade--racer-orange) ![engine](https://img.shields.io/badge/3D-Three.js-blue)

## Run it

```bash
npm install
npm run dev     # dev server (Vite) — open the printed URL
npm run build   # production bundle in dist/
```

## What's in the mine (design spec)

| Request | Implementation |
| --- | --- |
| **Mine with criss-crossing paths** | 15×15-cell maze (recursive backtracker) + 16% extra walls knocked out, so loops/cuts give multiple racing lines. Tunnels are 9 m wide, 5.4 m high. |
| **Roads & paving** | Straight tunnels use a procedural dirt-road texture with twin **wheel-rut grooves** (auto-rotated E/W vs N/S); intersections use packed-dirt with gravel. |
| **Grooves for mine carts** | 3 long rail lines thread the maze: recessed dark **ballast groove strip** in the floor, 1.5 m-gauge steel rails, wooden sleepers every 0.85 m. Corners are real curves (Catmull-Rom + tube geometry). |
| **Lights** | ~200 hanging cage lanterns (cord, cap, bars, glowing glass). A pool of the 6 nearest becomes real flickering point lights with fake volumetric cones; the rest stay emissive. Car has a shadow-casting headlight spot + warm fill. |
| **Beams / roof supports** | Classic timber sets everywhere: posts, lintels, knee-braces along corridors; chunky corner posts at intersections; planked ceilings over every tunnel. |
| **Maze to race** | Start headframe (SHAFT 07 sign) → 6 cyan holo-ring checkpoints (placed on the solution path, route itself is free) → gold chamber with checker flags. Live timer + per-seed best time. |
| **Carts pass by like traffic** | Each rail line has carts that wait a random interval, then shuttle end-to-end (and back). Rolling carts are lethal (3 hits = wreck); parked carts are soft obstacles. **Red flashing beacons** at the line ends + rhythmic clacking + HUD ⚠ warning telegraph them. |
| **Box car (slightly detailed)** | ~20-part box car: hood, cab + glass band, fenders, bumpers, roll bar, spare tyre, exhaust, head/tail lights; arcade physics with drift + handbrake, suspension bob/roll/pitch, chase & hood cameras. |

Extras for AAA-feel: ACES tonemapping, exponential fog, camera shake & speed-FOV,
damage vignette, 260 drifting dust motes, minimap, procedural WebAudio (engine,
sleeper-clacks by distance, warning bell, crashes, chimes) — **zero asset files**;
every texture (rock, dirt, road, wood, metal, sign, checker) is canvas-generated.

## Controls

- **WASD / arrows** — drive · **SPACE** — handbrake (drift)
- **R** — restart run · **C** — chase/hood camera · **M** — mute
- Touch controls appear automatically on mobile.

## Tuning (src/config.js)

- `MAZE_W/H`, `TILE`, `WALL_H`, `LOOP_CHANCE` — mine layout
- `RAIL_LINES`, `CART_SPEED`, `CART_HIT_RADIUS` — traffic danger
- `CAR.*` — acceleration, top speed, brakes · `HEALTH` — hits before wreck
- `CHECKPOINTS`, `LAMP_POOL`

Seeds: add `?seed=12345` to the URL to replay / share a specific mine layout.

## Code layout

```
src/config.js    tuning constants + tile<->world math
src/maze.js      maze generation, BFS solution path, rail-line picker
src/textures.js  procedural canvas textures
src/level.js     one-shot static world build (merged geometry, instancing)
src/car.js       box car model + arcade physics + wall collision
src/carts.js     minecart traffic system + warning beacons
src/audio.js     synthesized WebAudio (engine, clacks, chimes, crashes)
src/hud.js       DOM HUD, minimap, overlays
src/main.js      renderer, light pool, camera, race state machine, game loop
```
