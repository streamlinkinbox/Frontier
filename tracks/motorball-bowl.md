# SCRAPYARD BOWL — Motorball Circuit (Track 001)

Alita-inspired night bowl for FRONTIER. A 1.32 km banked concrete speed-bowl with
two animated trap-gate rings, floodlit grandstand straight, and a sky-city on the
horizon. Playable now: `app/motorball.html` (vanilla Three.js r160, no build step).

Key art: `app/assets/motorball-keyart.jpg` (also the loading-screen backdrop).

## 1. Reference breakdown (what the track is built from)

**Ref 1 — night bowl GIF.** Steep parabolic concrete banking, yellow/black hazard
chevrons along the top of the walls, tyre-marked concrete, floodlight gantry +
grandstand roof on one side, catch fencing, lone skater riding high in the bowl.
Translated to: bowl cross-section profile `y = 7.5·|u|^2.6` (56° max banking),
26 m wide rideable surface, canvas-generated concrete/tyre/chevron textures,
8 floodlight pylons, striped rim boards + chain-link fence, low-poly skater rigs.

**Ref 2 — Weta "Motorball Trap" concept (ring gate).** Giant rusted gunmetal ring
straddling the track, hydraulic side housings, top motor block, deployable spike
blades, yellow chevron road strip + edge lamps under the gate.
Translated to: `buildTrapGate()` — torus ring R16 m + cladding, side machinery,
3 rotating blade arms with spike tips and red tip lamps, chevron road patch,
edge strip lamps, green/amber/red state beacons + red warning light.

## 2. Layout (closed loop, 1320.8 m, clockwise from above)

Start/finish on the main straight (s = 0, gantry + checker line + grandstand).

| # | Sector / corner              | s (m)    | Character |
|---|------------------------------|----------|-----------|
| S1 | Main straight (start/finish) | 0 – 260  | Flat-out, grandstand left, Gate BETA guards the line at s ≈ 1268 |
| T1–T2 | East Bowl (180°)         | 260 – 450 | High-banked bowl, ride the wall at full speed |
| S2 | Back straight                | 450 – 800 | Gate ALPHA mid-straight (s ≈ 581), billboards infield |
| T3 | The Kink (esses)             | 800 – 850 | Tightest point (R ≈ 26 m) — lift, don't fight the slide |
| T4–T5 | West Bowl (180°)         | 900 – 1180 | Second bowl, sets up the run to the line |
| S3 | Run to the line              | 1180 – 1321 | Gate BETA (s ≈ 1268) → checker → lap |

Cross-section: 26 m wide bowl, 7.5 m walls, parabolic profile. Banking grips:
riding high (`|u| → 1`) cuts centrifugal slide by up to 55% — the fast line is
up in the bowl, exactly like the GIF.

## 3. Trap gates (gameplay)

- 3 blade arms per ring rotate continuously (ALPHA +0.85 rad/s, BETA −1.05 rad/s).
- **BLOCKED** while any arm is within 0.42 rad of bottom-dead-centre; **CYCLING**
  (amber) within 1.15 rad; otherwise **OPEN** (green). Beacons + HUD pill + map
  dots + audible alarm all mirror this state.
- Hit test: inside ±7 m of the gate while BLOCKED and on the road (`|u| < 0.92`)
  at speed → trap hit: speed ×0.32, full 360° spin, camera shake, spark burst,
  red vignette flash, 2.5 s immunity. Threading a CYCLING gate gives a near-miss
  whoosh + sparks, no penalty.
- HUD pill shows `TRAP GATE {name} · {state} · {distance}M` for the nearest gate
  within 110 m ahead.

## 4. Riders & rules

- 3 laps, standing start behind the line, 3-2-1-GO gantry lights, 4 riders
  (you + VOLT-9, JACKAL, MIRA-7 with rubber-banded AI pace ~56–60 m/s).
- Arcade physics: 76 m/s top speed, curvature-driven lateral slide vs banking
  grip, wall scrape (sparks + drag), reverse + wrong-way detection.
- Live standings (gap in metres), sector indicator, current/best lap, minimap
  with gates/AI/heading, finish panel with per-lap times, free-ride after finish.
- 4 cameras: chase (drag-look, wheel zoom), visor/onboard, trackside broadcast
  (6 auto-cutting cams), free orbit. `C` / `1–4`.
- Synthesised WebAudio: engine, wind, wall scrape, trap alarm, countdown, hit
  thud. `M` mutes. Touch controls on mobile.

## 5. Scene & art direction

Night, industrial, hazard-yellow accents on charcoal/steel-blue. Gradient sky
dome + stars, Zalem-style sky-city ring with elevator tether, 2 searchlights,
140-block scrapyard skyline with blinking beacons, 8 floodlight pylons
(4 real spotlights), 264 m grandstand with lit crowd, 4 emissive billboards,
container/infield clutter, catch fencing + striped rim boards all round.

## 6. File map

- `app/motorball.html` — page shell, HUD, menu/finish overlays, touch controls
- `app/motorball.js` — everything else (~1330 lines, sectioned; track math →
  textures → meshes → gates → riders → cameras → race logic → loop)
- `app/vendor/three.module.min.js` — vendored Three.js r160 (CDN fallback to
  jsdelivr → unpkg if the file is ever missing)
- `app/assets/motorball-keyart.jpg` — key art / loading backdrop
- `app/index.html` — lobby: Scrapyard Bowl is the first TRACKS entry; when a
  Motorball lobby goes live the client auto-loads `motorball.html`

## 7. Tuning knobs (in `motorball.js`)

`CONTROL` (centreline), `HALF_W`/`WALL_H`/`WALL_P` (bowl), `GATES[].f/speed`
(gate position/cycle), `TOP_SPEED`, slide `0.0026` + grip `0.55`, steer `0.95`,
`AI_DEFS[].base` (AI pace), `RACE_LAPS`.

## 8. Verifying geometry changes

The centreline length / corner radii can be checked headless (node + vendored
three, no browser needed) — see the measurement snippet in the build notes.
Current: 1320.8 m, tightest R 26.2 m at the kink (f ≈ 0.62), gates on R300+
ground (ALPHA f 0.44, BETA f 0.96).
