# Motorball Bowl — Scrapyard 99 (iteration 4)

Night banked Motorball oval for Frontier, built to the user's GIF reference
(floodlit bowl, yellow/black chevrons, cars pinned to high banked walls) plus
the Weta trap-gate reference and the Alita still (steel half-pipe, yellow
edge stripes). Playable Three.js, integrated with the lobby.

## Layout — analytic paperclip oval (L = 1281.76 m, lobby shows 1.28 km)

Exact analytic segments: dead-straight straights (κ = 0), exact R94.5 bowls.

| s (m) | segment |
|---|---|
| 0 – 272 | south straight A (x −100 → +172 at z = −94.5), start/finish at s = 0 |
| 272 – 569 | east bowl, centre (172, 0), R 94.5, apex x = 266.5 |
| 569 – 913 | north straight (x +172 → −172 at z = +94.5) |
| 913 – 1210 | west bowl, centre (−172, 0), R 94.5, apex x = −266.5 |
| 1210 – 1282 | south straight B (x −172 → −100), closes the loop |

## Jump cuts (iteration 4 — replaces the old prop ramps)

The road itself is cut, rally-style: the full 26 m width angles up over a
12 m runup (quadratic profile, 18° at the lip, 2 m tall), then nothing —
a 26 m open gap — then a steel landing wall. Steel cut faces span the full
width, yellow bars edge the lips (ref still), hazard paint on the runup,
chevrons on the landing zone, red glow in the cut, amber ticks on the map.

- Take off above ~23 m/s and you clear it (race pace sails 60–95 m).
- Too slow → INTO THE GAP reset before the lip.
- Short flight → face-plant into the landing wall (half speed, sparks).
- Landings scale by impact: shake, sparks, up to 5% speed scrub.
- Jumps at s = 140 (main straight) and s = 620 (back straight); AI fly them.

## Car physics — loaded bicycle model + simplified Pacejka (iteration 4)

Iteration 3's nose-heading model, made honest:

- Weight transfer: braking loads the front axle (turn-in bite), power
  loads the rear; per-axle load-sensitive μ (exponent −0.08).
- Combined slip: longitudinal demand eats lateral grip; rear-only
  power term, so greedy throttle on exit loosens the rear (RWD).
- Lateral load sensitivity from smoothed lateral accel.
- 600 kW power-curve engine (12.5 m/s² launch, tapering with speed),
  1.05·v² aero drag, rolling resistance, engine braking, ABS-capped
  brakes (24 m/s²), lip gradient resistance.
- Chassis feedback: body roll from lateral G, dive/squat from
  longitudinal G, lip/flight pitch, speed-scaled suspension bob,
  tire-skid audio from smoothed slip angle.

## Validated pace (headless sim, exact game constants)

- 0–60 in 6.3 s, 0–76 in 13.5 s; brakes 76→60 in 45 m.
- Bowls: comfortable 60–65, limit ≈ 66 (progressive wide slide past it).
- Full lap from standstill with jumps: zero walls, zero gap fails,
  nose ≤ 14°, slip ≈ 5°. No NaN in any run.

Brake for the bowls, pin the jumps, dodge ALPHA (s = 740) and BETA (s = 1235).
