# Motorball Bowl — Scrapyard 99 (iteration 3)

Night banked Motorball oval for Frontier, built to the user's GIF reference
(floodlit bowl, yellow/black chevrons, cars pinned to high banked walls) plus
the Weta trap-gate reference. Playable Three.js, integrated with the lobby.

## Layout — analytic paperclip oval (L = 1281.76 m, lobby shows 1.28 km)

No spline: the centreline is exact analytic segments, so straights are
dead straight (κ = 0) and bowls are exact circles (R = 94.5 m, κ = 0.01058):

| s (m) | segment |
|---|---|
| 0 – 272 | south straight A (x −100 → +172 at z = −94.5), start/finish at s = 0 |
| 272 – 569 | east bowl, centre (172, 0), R 94.5, apex x = 266.5 |
| 569 – 913 | north straight (x +172 → −172 at z = +94.5) |
| 913 – 1210 | west bowl, centre (−172, 0), R 94.5, apex x = −266.5 |
| 1210 – 1282 | south straight B (x −172 → −100), closes the loop |

Both bowls are right-handers. History: iterations 1–2 used a CatmullRom
spline whose straight-to-bowl joints pinched to R ≈ 71 m; iteration 3
replaced it with this analytic builder (min R now exactly 94.5 m, verified).

## Walls, banking, looks

- `bankAt`: smoothed |κ| (800 samples, ±50 m boxcar), thresholds 0.003/0.007,
  wall height 0.7 m (straights) → 10 m (bowls), smooth ramps at joints.
- Bowl dish exponent WALL_P = 2.2; physics bank push is 1.55× the visual
  gradient (`bowlSlope` gain 3.4, |u|^1.2) so cars can ride the high wall.
- Shoulder chevron bands on the banking, chevron ramp tops, edge lamps,
  floodlight glow, trap gates ALPHA (s = 740, mid north straight) and
  BETA (s = 1235, 47 m before the line), ramps at s = 620 (right lane)
  and s = 140 (left lane).

## Car physics — nose-heading bicycle model + simplified Pacejka

Iteration 3 killed the Subway Surfers feel: steering now yaws a true nose
heading `hErr` (car yaw relative to road), and velocity follows the nose:

- `hErr += (yawRate − v·κ)·h`, clamped ±1.05 rad
- `vLat = v·sin(hErr) + vy·cos(hErr)`, road load rotated by `cos(hErr)`
- Pacejka `sin(1.35·atan(11·α))` per axle, μ = 1.7, downforce `0.005·v²`,
  friction-circle longitudinal scaling, 120–180 Hz substeps
- Steer authority `dMax = min(0.6, 3.4/max(v,10))` — smooth 1/v curve, no
  cliffs; full lock at speed will spin you, like a real car
- Yaw damping `2.0 + 0.03·v`; physics κ reads a ±24 m smoothed heading
  table (`sHeadS`, clothoid-like easements at joints, seam-safe)
- Chase/visor cameras, minimap tick and AI all follow the nose heading

## Validated pace (headless sim, PD + lookahead driver, exact game constants)

- Straights: 76 m/s, dead stable, 0.0° nose error
- Bowls: comfortable 60–65, limit ≈ 66 (progressive wide slide past it)
- Full laps 76 + braking to 61–63: zero wall hits, nose ≤ 13°, slip ≈ 4°
- No NaN in any run; limit behaviour is a gentle understeer plow, not a cliff

Brake for the bowls (keys S / ↓), ride the banking, dodge the trap gates.
Runtime feel still needs a human playtest — numbers can't feel fun.
