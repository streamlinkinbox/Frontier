# Motorball Circuit — "Scrapyard Loop" (iteration 9)

Wipeout-style twisty circuit. The oval is gone: a 14-vertex rounded-polygon
centreline (trimmed straights + tangent arc fillets, exact closure by
construction) with an S-bite, a 138° hairpin, a hill climb, and final esses.
Three fork zones — including a 3-way TRIDENT split with bridge / gauntlet /
dip arms — plus 5 rally jump cuts, 2 trap gates, and full elevation (0–12.8 m).

- Lap: **2182.3 m** · s = 0 mid home straight (start/finish + gantry + grid)
- File: `app/motorball.js?v=9` · physics: per-axle Pacejka MF (unchanged) +
  terrain grade forces + elevation-aware airtime
- Validation: 30/30 headless checks pass (see bottom)

## Corner / straight table (game s, metres)

| s range | what | detail |
|---|---|---|
| 1990→193 | E0 home straight | flat, FORK-A Y-split [35, 175], grid + gantry |
| 193–278 | V1 sweeper | −54°, R90, climb begins |
| 278–533 | S-bite E1–E4 | −36° / +72° / −36° (R60/38/60), climbing esses |
| 486–533 | E4 stem | GATE BETA @509 |
| 533–581 | V5 | −55°, R50 |
| 581–770 | E5 back straight | TRIDENT 3-way [600, 750], crest 12 m @675 |
| 770–822 | V6 | −55°, R55, descent |
| 822–911 | E6 stem | JUMP @865 (gap 24) |
| 911–967 | V7 | −27°, R120 kink |
| 967–1056 | E7 stem | JUMP @1010 downhill (gap 26) |
| 1056–1125 | V8 | −99°, R40, hairpin entry |
| 1125–1156 | E8 connector | valley floor (y≈1) |
| 1156–1210 | V9 HAIRPIN | +139°, R22, slowest corner (~27 m/s AI) |
| 1210–1343 | E9 hill climb | FORK-C Y-split [1220, 1325], grade ≈6.5% |
| 1343–1378 | V10 | −40°, R50 |
| 1378–1429 | E10 crest | JUMP @1394 (gap 24), summit 12.8 m, downhill landing |
| 1429–1581 | V11 | −87°, R100, descent |
| 1581–1646 | E11 stem | GATE ALPHA @1613 |
| 1646–1844 | final esses | +45° / −44° (R90/R90) with rollers |
| 1844–1990 | V0 | −83°, R100 onto home straight |

## Fork zones (stem cut, arms laid instead)

| zone | s | arms (left → right) |
|---|---|---|
| FORK-A (Y) | 35–175 | SPEEDWAY (sweeper +12 m) / SKYLINE (S-curves) |
| TRIDENT (3-way) | 600–750 | SUMMIT (bridge +3.5 m, jump @675) / GAUNTLET (flat, jump @682) / ABYSS (dip −3 m) |
| FORK-C (Y) | 1220–1325 | SURGE (outer sweeper) / DIVE (inner S) |

Arm choice: player's line at the split (halves / thirds); AI cycles arms per
lap and drifts to the mouth. Funnel mouths (±3.5 m flare) keep reframes
position-continuous; gores + named direction gantry per split.

## Jumps (full-width angled lips, rally cuts)

| lip | where | gap | notes |
|---|---|---|---|
| 865 | E6 stem | 24 m | back-section kicker |
| 1010 | E7 stem | 26 m | downhill launch at the hairpin |
| 675 | SUMMIT arm | 24 m | kicker off the bridge crest |
| 682 | GAUNTLET arm | 22 m | flat-out centre jump |
| 1394 | E10 stem | 24 m | crest jump, downhill landing into V11 braking |

Airtime is elevation-aware: flight height is measured against the live road
plane (takeoff ref stored at the lip), so downhill landings and bridge jumps
behave. Min clear speed ≈ 25 m/s; AI pace clears everything.

## Elevation

Linear keys, max stem grade 6.5% (E9 climb), max combined (bridge ramp on
climb) 10.5%. Valley (y≈1) at the hairpin, summit 12.8 m at E10 crest, home
straight dead flat. Grade forces slow climbs / reward descents; chassis pitch
follows the terrain.

## Behaviour notes

- AI: corner slowdown from braking-horizon curvature (hairpin ≈ 27 m/s,
  sweepers ≈ 55–62, straights at base 56–60), per-zone arm plans.
- Banking rescaled for corner radii (R120 ≈ 3 m dish → R50+ = 10 m towering
  walls); spiral walls now on BOTH sides (canyon); stem dish flattened inside
  fork zones so arm blends stay smooth.
- Gates ALPHA (1613) / BETA (509) live on stems; legs extend to the ground
  from elevated road. Trap logic unchanged (route 0 only).
- Minimap draws all 7 arms in arm colours; bounds auto-fit.

## Headless validation (`.arena/scratch/harness9.mjs`, real game math)

30/30 PASS: lap 2182.29 m, closure 0.0000 m, no-NaN full-lap sweep (stems +
arms), mouth continuity d=0.000 m / Δhead ≤ 0.22°, all 5 jumps CLEAR at
25/45/65 m/s, zone-placement rules, plus 3 rollout laps (left arms 47.2 s,
right arms 47.8 s, trident-centre 47.2 s) with every taken jump flown, no gap
falls.
