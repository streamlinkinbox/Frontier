# SCRAPYARD BOWL — Motorball Circuit (Track 001)

Alita-inspired night circuit for FRONTIER. A 1.32 km bowl circuit with **flat
straights and huge banked turns** (up to 63°), two animated trap-gate rings,
jump ramps, and emissive edge lighting pulled straight from the reference GIF.
Playable now: `app/motorball.html` (vanilla Three.js r160, no build step).

Key art: `app/assets/motorball-keyart.jpg` (also the loading-screen backdrop).

## 1. Reference breakdown (what the track is built from)

**Ref 1 — night bowl GIF.** Steep banked concrete in the turns, yellow/black
hazard chevrons, tyre-marked concrete, **rows of emissive dots marching down
both road edges**, floodlight gantry + grandstand roof, catch fencing.
Translated to: curvature-driven banking (0.7 m dish on straights → 10 m walls
in turns), canvas-generated concrete/tyre/chevron textures, ~330 instanced
emissive edge lamps + additive glow lines, 8 floodlight pylons, striped rim
boards + chain-link fence.

**Ref 2 — Weta "Motorball Trap" concept (ring gate).** Giant rusted gunmetal
ring straddling the track, hydraulic side housings, top motor block,
deployable spike blades, yellow chevron road strip + edge lamps under the gate.
Translated to: `buildTrapGate()` — torus ring R16 m + cladding, side machinery,
3 rotating blade arms with spike tips and red tip lamps, chevron road patch,
edge strip lamps, green/amber/red state beacons + red warning light.

## 2. Layout (closed loop, 1320.8 m)

Start/finish on the main straight (s = 0, gantry + checker line + grandstand).

| # | Sector / corner          | s (m)    | Banking | Character |
|---|--------------------------|----------|---------|-----------|
| S1 | Main straight            | 0 – 260  | 8°      | Flat-out; jump ramp left lane (s 120); Gate BETA guards the line (s 1268) |
| T1–T2 | East Bowl (180°)     | 260 – 450 | → 63°  | High-banked bowl, ride the wall at 55–65 m/s |
| S2 | Back straight            | 450 – 800 | 8–17°  | Gate ALPHA (s 581); jump ramp right lane (s 700) |
| T3 | The Kink (esses, R 26 m) | 800 – 850 | 63°   | Brake to ~30 m/s — the wild sliding section |
| T4–T5 | West Bowl (180°)     | 900 – 1180 | → 63° | Second bowl, sets up the run to the line |
| S3 | Run to the line          | 1180 – 1321 | 8°   | Gate BETA → checker → lap |

Banking is computed per-sample from smoothed track curvature (`bankAt(s)`):
straights ≈ flat (0.7 m dish), bowls/kink = 10 m walls at up to 63.4°.
Cross-section stays 26 m wide with a parabolic profile.

## 3. Cars & simplified Pacejka

Skaters are out — 4 low-poly **race cars** (player + VOLT-9, JACKAL, MIRA-7)
with spinning wheels, steered front axle, wing, glass cabin, headlights,
tail-light bar and spotlight.

Player physics is a bicycle-model **simplified Pacejka ("magic formula")**:

- Per-axle lateral force `F = −μ·m·N·½·pacejka(α)`,
  `pacejka(α) = sin(1.35·atan(11·α))`, slip angles from lateral velocity +
  yaw rate + steer angle; μ = 1.4.
- Load `N = g + 0.004·v²` (gravity + aero downforce), weight transfer implicit
  via the friction circle: longitudinal force scaled by
  `√(1 − Flat²)` — trail-brake and throttle-on-exit matter.
- Track frame adds centrifugal `v²·κ` vs banking gravity `g·slope(u,s)`:
  the fast line rides high where the wall holds the car.
- Sub-stepped at 120–180 Hz for stability; HUD lat-G bar shows live tyre load.
- Verified headless against the real curvature profile: straights flat-out at
  76 m/s · bowls hold 55–65, grind the wall past ~70 · kink needs ~30 m/s.

## 4. Trap gates (gameplay)

- 3 blade arms per ring rotate continuously (ALPHA +0.85 rad/s, BETA −1.05).
- **BLOCKED** while any arm is within 0.42 rad of bottom-dead-centre;
  **CYCLING** (amber) within 1.15 rad; otherwise **OPEN** (green). Beacons +
  HUD pill + map dots + audible alarm all mirror this state.
- Hit test: inside ±7 m of a BLOCKED gate, on the road, at speed, **and on the
  ground** — catch air off a ramp and you can fly clean under the blades.
  Hit = speed ×0.32, full 360° spin, shake, spark burst, red flash, 2.5 s
  immunity. Threading a CYCLING gate gives a near-miss whoosh, no penalty.

## 5. Ramps & airtime

Two hazard-striped jump ramps in optional straight lanes (s 120 left, s 700
right): 13 m long, 1.7 m lip with a glowing yellow exit bar. Launch velocity
scales with speed (capped), 16 m/s² arcade gravity, landing sparks + thud +
shake scaled by impact, reduced control while airborne. No tyre forces in the
air — line up before the lip.

## 6. Rules, cameras, audio

- 3 laps, standing start, 3-2-1-GO gantry lights; live standings (metre gaps),
  sector indicator, current/best lap, minimap with gates/AI/heading, finish
  panel with per-lap times, free-ride after finish.
- 4 cameras: chase (drag-look, wheel zoom), visor/onboard, trackside broadcast
  (6 auto-cutting cams), free orbit. `C` / `1–4`.
- Synthesised WebAudio: engine, wind, wall scrape, trap alarm, countdown, hit
  thud. `M` mutes. Touch controls on mobile.

## 7. Scene & art direction

Night, industrial, hazard-yellow on charcoal/steel-blue. Gradient sky dome +
stars, Zalem-style sky-city ring with elevator tether, 2 searchlights,
140-block scrapyard skyline with blinking beacons, 8 floodlight pylons
(4 real spotlights), 264 m grandstand with lit crowd, 4 emissive billboards,
container/infield clutter, catch fencing + striped rim boards all round.

## 8. File map

- `app/motorball.html` — page shell, HUD (speed + lat-G bar), menu/finish
  overlays, touch controls
- `app/motorball.js` — everything else (~1500 lines: track math → banking →
  textures → meshes → lamps/ramps/gates → cars → Pacejka → cameras → loop)
- `app/vendor/three.module.min.js` — vendored Three.js r160 (CDN fallback to
  jsdelivr → unpkg if the file is ever missing)
- `app/assets/motorball-keyart.jpg` — key art / loading backdrop
- `app/index.html` — lobby: Scrapyard Bowl is the first TRACKS entry; when a
  Motorball lobby goes live the client auto-loads `motorball.html`

## 9. Tuning knobs (in `motorball.js`)

`CONTROL` (centreline) · `BANK_MIN`/`BANK_MAX` + curvature thresholds
`0.0035`/`0.011` (banking) · `GATES[].f/speed` (gates) · `RAMPS[]`
(ramps) · Pacejka `B=11, C=1.35, μ=1.4`, downforce `0.004`, `TOP_SPEED` ·
`AI_DEFS[].base` (AI pace) · `RACE_LAPS`.

Banking + Pacejka balance can be re-verified headless (node + vendored three,
no browser): replicate `bankAt` from `CONTROL`, then integrate the tyre core
over the curvature profile — bowls should hold ~60, walls past ~70, kink ~30.
