# Motorball Bowl — Scrapyard 99 (iteration 5)

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
| 569 – 913 | north straight (x +172 → −172 at z = +94.5) |
| 913 – 1210 | west bowl, centre (−172, 0), R 94.5, apex x = −266.5 |
| 1210 – 1282 | south straight B (x −172 → −100), closes the loop |

## Spiral turn walls (iteration 5 — the turns, not corners)

The bowls are now half-pipe spirals like the stills. A Bezier-profile steel
wall rings each turn where the banking is high, growing with `bankAt`:

- Outer wall: 24 m above the road edge (~34 m absolute), leaning out to
  16.75 m off-centre at mid-height, then curling back to 14 m at the top
  so it looms over the track. Chevron ring crowns the top quarter.
- Inner wall: low 6 m steel rim, chevron-capped, infield stays visible.
- Chain-link fence + posts yield where the wall stands (rim + rail stay).
- 4 floodlight pylons at the bowl ends (43 m heads, volumetric-look cones).
- Trackside bowl cameras moved above the wall (44 m out, 40 m up) —
  stadium view down into the spiral.

Purely visual: road, banking profile and physics are untouched, so all
iteration-4 handling validation still stands.

## Jump cuts (iteration 4)

The road is cut rally-style: full 26 m width angles up over a 12 m runup
(18° lip, 2 m tall), 26 m open gap, steel landing wall. Steel cut faces,
yellow lip bars, hazard runup, landing chevrons, red gap glow, map ticks.

- Clear speed ≈ 23 m/s (race pace sails 60–95 m); slow → gap reset;
  short → landing-wall face-plant; impact-scaled landings. AI fly them.
- Jumps at s = 140 (main straight) and s = 620 (back straight).

## Car physics — loaded bicycle model + simplified Pacejka (iteration 4)

Nose-heading model: weight transfer (brake bite, power squat), per-axle
load-sensitive μ, combined slip with RWD power oversteer, lateral load
sensitivity, 600 kW power curve, aero drag, ABS brakes, engine braking,
lip gradient, chassis roll/dive/squat, flight pitch, skid audio.

## Validated pace (headless sim, exact game constants)

- 0–60 in 6.3 s, 0–76 in 13.5 s; brakes 76→60 in 45 m.
- Bowls: comfortable 60–65, limit ≈ 66 (progressive wide slide past it).
- Full lap from standstill with jumps: zero walls, zero gap fails,
  nose ≤ 14°, slip ≈ 5°. No NaN in any run.

Brake for the bowls, pin the jumps, dodge ALPHA (s = 740) and BETA (s = 1235).
