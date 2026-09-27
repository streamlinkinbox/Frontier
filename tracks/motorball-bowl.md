# Motorball Bowl — Scrapyard 99 (iteration 8)

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
| 569 – 913 | north straight (x +172 → −172 at z = +94.5), Y-junction 625–850 |
| 913 – 1210 | west bowl, centre (−172, 0), R 94.5, apex x = −266.5 |
| 1210 – 1282 | south straight B (x −172 → −100), closes the loop |

## Spiral turn walls (iteration 5, de-duplicated in 6 — one treatment, once)

The bowls are half-pipe spirals like the stills. A Bezier-profile steel
wall rings each turn where the banking is high, growing with `bankAt`:

- Outer wall: 24 m above the road edge (~34 m absolute), leaning out to
  16.75 m off-centre at mid-height, then curling back to 14 m at the top
  so it looms over the track. Chevron ring crowns the top quarter —
  the ONLY chevron treatment on the circuit now.
- Chain-link fence + posts yield only where the outer wall stands.
- 4 floodlight pylons at the bowl ends (43 m heads, volumetric-look cones).
- Trackside bowl cameras above the wall (44 m out, 40 m up) —
  stadium view down into the spiral.

## Y-junction fork (iteration 8 — proper split tracks, not a divided straight)

The north straight genuinely forks at s = 625 and rejoins at s = 850.
The stem road is cut through the zone and two separate 16 m carriageways
with real bends are laid instead (own rails, own edge glow, tangent-
continuous ends, position-continuous reframe at fork/rejoin):

- LEFT arm — SPEEDWAY (cyan): fast outer sweeper bulging 18 m off the
  stem line, banked, flat out at 76. Carries a flat-out kicker jump
  (lip s = 730, full arm width).
- RIGHT arm — SKYLINE (orange): infield S-curve rhythm section, mild
  bank, flows ~60. Carries its own rhythm jump (lip s = 700).
- Fork furniture: amber crash-gore nose + barrels at both ends,
  overhead gantry sign (◀ SPEEDWAY ║ SKYLINE ▶), both arms drawn on
  the minimap in their colours. Route banner calls your arm at the fork.
- Your side at the gore picks your arm; AI drift to their arm mouth
  and alternate arms per lap.
- Both arms fly their jumps and rejoin with 60+ m of west-bowl setup.
  Headless laps: SPEEDWAY 22.9 s, SKYLINE 23.0 s — both genuinely viable.

## Jump cuts (iteration 4, route-gated in 8)

The road is cut rally-style: full width angles up over a 12 m runup
(18° lip, 2 m tall), 26 m open gap, steel landing wall. Steel cut faces,
yellow lip bars, hazard runup, landing chevrons, red gap glow, map ticks.
Jumps are route-gated: each cut exists only on its own carriageway.

- Clear speed ≈ 23 m/s (race pace sails 60–95 m); slow → gap reset;
  short → landing-wall face-plant; impact-scaled landings. AI fly them.
- Jumps at s = 140 (main straight), s = 730 (SPEEDWAY arm) and
  s = 700 (SKYLINE arm).

## Car physics — loaded bicycle model + Pacejka Magic Formula (iteration 8)

Nose-heading model: weight transfer (brake bite, power squat), per-axle
load-sensitive μ, combined slip with RWD power oversteer, lateral load
sensitivity, 600 kW power curve, aero drag, ABS brakes, engine braking,
lip gradient, chassis roll/dive/squat, flight pitch, skid audio.
Steering: 8/v authority curve (full lock holds the bowls with ~20%
margin — excess lock plows, never spins), smooth 5.5/6 Hz hands for
keyboard input, realistic yaw inertia (IZ 2400).

Tires are the full Pacejka Magic Formula now, per axle —
F = D·sin(C·atan(B·α − E·(B·α − atan(B·α)))), D from μ·load:

| axle | B (stiffness) | C (shape) | E (curvature) | character |
|---|---|---|---|---|
| front | 11 | 1.35 | +0.25 | crisp, communicative breakaway, peak @ 14° |
| rear | 10 | 1.40 | −0.20 | forgiving tail, pulls through slides, peak @ 11° |

Past-peak fall-off is gentle (front holds 0.97 @ 30° slip), so slides
are progressive and catchable — the limit feels like a tire, not a cliff.

## Validated pace (headless sim, exact game constants)

- 0–60 in 6.3 s, 0–76 in 13.5 s; brakes 76→60 in 45 m.
- Bowls: comfortable 60–65, limit ≈ 66 (progressive wide slide past it).
- Full lap per Y arm from standstill: SPEEDWAY zero incidents, flights
  64 m + 86 m; SKYLINE zero incidents, flights 64 m + 85 m;
  nose ≤ 17°, slip ≈ 8°. Full lock pinned at 76 slides but never spins.
  No NaN in any run.

Brake for the bowls, pick your arm at the gore, pin the jumps,
dodge ALPHA (s = 60) and BETA (s = 1235).
