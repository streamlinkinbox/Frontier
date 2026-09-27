# Motorball Bowl — Scrapyard 99 (iteration 6)

Night banked Motorball oval for Frontier, built to the user's GIF reference
(floodlit bowl, yellow/black chevrons, cars pinned to high banked walls) plus
the Weta trap-gate reference and the Alita stills (steel half-pipe, yellow
edge stripes, towering spiral turns). Playable Three.js, integrated with the
lobby.

## Layout — analytic paperclip oval (L = 1281.76 m, lobby shows 1.28 km)

Exact analytic segments: dead-straight straights (κ = 0), exact R94.5 bowls.

| s (m) | segment |
|---|---|
| 0 – 272 | south straight A (x −100 → +172 at z = −94.5), start/finish at s = 0 |
| 272 – 569 | east bowl, centre (172, 0), R 94.5, apex x = 266.5 |
| 569 – 913 | north straight (x +172 → −172 at z = +94.5), split track 656–830 |
| 913 – 1210 | west bowl, centre (−172, 0), R 94.5, apex x = −266.5 |
| 1210 – 1282 | south straight B (x −172 → −100), closes the loop |

## Spiral turn walls (iteration 5, de-duplicated in 6 — one treatment, once)

The bowls are half-pipe spirals like the stills. A Bezier-profile steel
wall rings each turn where the banking is high, growing with `bankAt`:

- Outer wall: 24 m above the road edge (~34 m absolute), leaning out to
  16.75 m off-centre at mid-height, then curling back to 14 m at the top
  so it looms over the track. Chevron ring crowns the top quarter —
  the ONLY chevron treatment on the circuit now.
- Removed in 6: the inner 6 m rim (fence restored on the infield side)
  and the road shoulder hazard stripes — the turn walls carry the look.
- Chain-link fence + posts yield only where the outer wall stands.
- 4 floodlight pylons at the bowl ends (43 m heads, volumetric-look cones).
- Trackside bowl cameras above the wall (44 m out, 40 m up) —
  stadium view down into the spiral.

## Split track (iteration 6)

The north straight forks at s = 656 and rejoins at s = 830 around a
174 m steel barrier island (smooth fork/rejoin tapers, amber lamp strip,
beacons on both tips, amber line on the minimap):

- LEFT path: smooth and clean, no jump — the safe line.
- RIGHT path: carries its own lane jump (lip s = 720, 26 m cut over
  u ∈ [−1, −0.2] only) — risk/reward.
- Flush fork: the old full-width back-straight jump was deleted so the
  81 m run from the bowl exit is clean tarmac to pick a side on, and the
  rejoin lands 80 m before the west bowl for corner setup.
- Island collision (push + scrape sparks, head-on thud + 10% scrub);
  AI alternate sides per lap and fly the lane jump.

## Jump cuts (iteration 4, lane-scoped in 6)

The road is cut rally-style: full 26 m width angles up over a 12 m runup
(18° lip, 2 m tall), 26 m open gap, steel landing wall. Steel cut faces,
yellow lip bars, hazard runup, landing chevrons, red gap glow, map ticks.
Jumps are lane-gated (`inLane`): only the 720 cut is lane-scoped.

- Clear speed ≈ 23 m/s (race pace sails 60–95 m); slow → gap reset;
  short → landing-wall face-plant; impact-scaled landings. AI fly them.
- Jumps at s = 140 (main straight, full width) and s = 720 (right
  split lane only).

## Car physics — loaded bicycle model + simplified Pacejka (iteration 4)

Nose-heading model: weight transfer (brake bite, power squat), per-axle
load-sensitive μ, combined slip with RWD power oversteer, lateral load
sensitivity, 600 kW power curve, aero drag, ABS brakes, engine braking,
lip gradient, chassis roll/dive/squat, flight pitch, skid audio.

## Validated pace (headless sim, exact game constants)

- 0–60 in 6.3 s, 0–76 in 13.5 s; brakes 76→60 in 45 m.
- Bowls: comfortable 60–65, limit ≈ 66 (progressive wide slide past it).
- Full lap per split path from standstill: LEFT zero incidents, 1 flight
  (64 m); RIGHT zero incidents, 2 flights (64 m + 86 m lane jump);
  nose ≤ 16°, slip ≈ 5°. No NaN in any run.

Brake for the bowls, pick a side at the fork, pin the jumps,
dodge ALPHA (s = 60) and BETA (s = 1235).
