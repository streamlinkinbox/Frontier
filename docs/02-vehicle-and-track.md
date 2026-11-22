# 02 — Vehicle, Surfaces, and the Natural Corridor

---

## 1. The vehicle: "CORVID" — Class-J Jungle Racer

A stripped competition truck: tube-frame chassis, mesh grille, long-travel
suspension, a roof hoop, a front hydraulic cutter roller, and a lot of exposed
hardware for the vines to grab. It is *not* a supercar and *not* a toy — it is
a working machine that a crew maintains with zip ties and swearing.

### 1.1 Specification

| Item | Value |
|---|---|
| Kerb mass | 1 450 kg (incl. 60 kg driver) |
| Mass distribution | 54 % front / 46 % rear |
| Wheelbase | 2.95 m |
| Track (F / R) | 1.68 m / 1.66 m |
| CoG height | 0.52 m |
| Yaw inertia `Iyy` | 2 180 kg·m² |
| Roll inertia `Ixx` | 560 kg·m² |
| Pitch inertia `Izz` | 620 kg·m² |
| Tyres | 265/60 R17, radius 0.385 m, `Iw` 1.6 kg·m² |
| Front / rear static load | 770 / 680 kg |
| Steering rack | 34° lock, speed-sensitive limit (§1.6) |
| Engine | 3.0 L turbo I6, 420 hp @ 6 200, 480 Nm @ 3 400 |
| Gearbox | 6-speed sequential, 3.73 / 2.19 / 1.54 / 1.21 / 1.00 / 0.82 |
| Drivetrain | 4WD, 40 : 60 front/rear torque split, viscous centre LSD |
| Aero | `Cd·A` 1.15 m², `Cl·A` 0.55 front / 0.95 rear |
| Brakes | 320 mm discs, 68 % front bias; handbrake on rear only |
| Electrical | 200 A alternator (drives the cutter roller) |

### 1.2 Suspension

| Corner | Spring rate | Travel | Bump stop | Damper (bump / rebound) | ARB |
|---|---|---|---|---|---|
| Front | 38 kN/m | 220 mm | 9 kN/m progressive | 3.6 / 5.4 kN·s/m | 18 kN/m |
| Rear | 42 kN/m | 240 mm | 9 kN/m progressive | 4.0 / 6.1 kN·s/m | 12 kN/m |

**Model.** Each corner is a raycast strut against the world. The strut's
**fully extended length is its rest length** (227 mm): past that the wheel is
in the air and the tyre carries nothing, which is what stops a landing from
loading a tyre that is not touching anything. Compression `x` from rest →
spring force `Fs = k·x + c·ẋ`, then the **progressive bump stop**:

```
 over      = rawCompression − (travel − 30 mm)
 FbumpStop = 7.5 MN/m · over²          (soft at first, rigid at the end)
```

then a **traction-limited** clamp: the tyre force can never exceed `μ·Fz`, and
`Fz` comes from the strut, so the chassis genuinely unloads over bumps and the
tyres genuinely let go. Anti-roll bars couple left/right compression
differentially. Spring loads are applied **at the contact patch**, not at the
centre of mass, so load transfer is a consequence of the geometry rather than
a fudge factor.

**Feel target.** The body should show 3–6° of roll in a hard corner and 2–4° of
squat/dive under braking, visible at the camera distance. Weight transfer must
be legible *before* the yaw response — that is what lets the player catch a
slide.

### 1.3 Tyre model

A simplified **Pacejka Magic Formula** with combined slip via a friction
ellipse, because "the car slides sideways in a fun way" is not enough — the
*shape* of the slide must be physically consistent so that counterplay works.

**Longitudinal** (`κ` = slip ratio, `Fz` = vertical load):

```
Fy = D · sin( C · atan( B·α − E·(B·α − atan(B·α)) ) )
```

| Coefficient | Value |
|---|---|
| `B` (stiffness) | 9.4 (long.) / 8.1 (lat.) |
| `C` (shape) | 1.62 (long.) / 1.38 (lat.) |
| `E` (curvature) | 0.97 |
| `D` (peak) | `μ_surface · Fz · loadSensitivity(Fz)` |
| Load sensitivity | `1 − 0.11·(Fz/Fz₀ − 1)`, `Fz₀` = 3 800 N |

**Combined slip.** Compute `σx = κ/κ_peak`, `σy = tan(α)/tan(α_peak)`, then
`σ = √(σx² + σy²)`; the resultant magnitude is scaled by `f(σ)` and the
direction is the vector `(σx, σy)`. This is what produces the correct
behaviour: **stomp the throttle mid-corner and the front steps out** (because
longitudinal demand eats the friction ellipse).

**Wheel dynamics.** Each wheel has rotational state:
`Iw·dω/dt = T_drive − T_brake·sign(ω) − Fx·r`. This gives wheelspin,
lock-up, and engine braking for free, and it is what makes the handbrake a
real tool.

### 1.4 Powertrain

- Engine torque from a **measured curve** (not a multiplier), interpolated at
  200 rpm points: 180 Nm @ 1 000 → 480 Nm @ 3 400 → 430 Nm @ 4 800 →
  300 Nm @ 6 000 → 205 Nm @ 7 200.
- Turbo: 0.35 s spool lag (torque multiplier ramps 1.0 → 1.35 with a first-order
  lag), and **anti-lag** keeps 15 % boost on lift for 0.8 s (it is a jungle truck,
  it bangs).
- Gearbox: sequential, 120 ms shift time, torque cut during shift, auto-shift on
  rpm with a configurable aggressiveness (default: shift at 92 % redline, 4 500
  rpm downshift).
- 4WD split 40:60 with a viscous centre coupling (40 % of the transmitted
  torque is speed-dependent, up to 70 % locked at low rpm) — this gives the
  vehicle the traction to fight a snare, but it also means the *front* is
  dragged along when a snare catches it, which is the intended frustration.
- Diff lock 100 % on the rear, 60 % on the front — enough to climb out of mud
  shelves, not enough to make it a tractor.

### 1.5 Aerodynamics

```
 Fdrag = ½·ρ·(Cd·A)·|v|·v            ρ = 1.225
 Fdown = ½·ρ·(Cl·A)·v²  applied at the front/rear aero centres
```

At 33 m/s (120 km/h): drag ≈ 821 N, rear downforce ≈ 637 N, front ≈ 369 N.
Downforce is scaled by a **surface factor** — a claw pad on the roof reduces it
by up to 25 %, which the player feels as the car going loose.

### 1.6 Steering

| Speed | Max steer angle |
|---|---|
| 0 km/h | 34° |
| 30 km/h | 28° |
| 60 km/h | 19° |
| 100 km/h | 12° |
| 140 km/h | 8° |

The rack is **speed-sensitive by rack travel**, not by input scaling: at high
speed the same physical rack travel produces less angle, so the wheel in your
hands still moves the same distance (important: no numb steering wheel).
Ackermann correction of 14 %. Countersteer assist is **off** by default — the
game never takes the steering away from the player.

### 1.7 Integration & solver

- Fixed step **240 Hz**, accumulator-driven, max 8 substeps per frame, render
  interpolation between states (so 60/120/144 Hz displays all behave the same).
- Semi-implicit Euler for the body; per-corner forces accumulated into a single
  force/torque pair; angular integration in body space with the full inertia
  tensor; quaternion renormalised each step.
- **Vine constraints are solved in the same accumulator** as the tyres, after
  the tyre forces but before integration, using a sequential-impulse solve with
  8 iterations and a Baumgarte stabilisation factor of 0.2 and 1 mm of allowed
  penetration. This is why a vine pull produces a *real* yaw impulse rather than
  a scripted rotation.

---

## 2. Surfaces

| Surface | μ | Rolling resist. | Sinkage | Notes |
|---|---|---|---|---|
| Packed dirt | 1.05 | 0.020 | 0 mm | The baseline ribbon |
| Dry gravel | 1.25 | 0.028 | 5 mm | Fast sections, good drainage |
| Wet mud | 0.72 | 0.045 | 18 mm | Traction sinkhole; the classic trap |
| Leaf litter | 0.85 | 0.035 | 8 mm | Beautiful, hides roots |
| Root shelf | 0.95 | 0.030 | 0 mm | Raised roots = suspension events |
| Standing water | 0.55 | 0.090 | 40 mm | Genuinely dangerous, rare |
| Rock slab | 1.40 | 0.012 | 0 mm | Fast, unforgiving of bumps |

Surface transitions are blended over 1.5 m, and the **blend is visible** (colour,
debris, spray) so the player can read it — the single most important fairness
rule in the whole track design.

---

## 3. The natural corridor (why it is not three lanes)

### 3.1 The generator

The path is a **Catmull-Rom spline** through control points generated by a
layered noise field, sampled at 1 m into a polyline of ~3 000 points for a
3 km run.

| Property | Generation |
|---|---|
| Curvature | Two octaves of value noise on heading (0.004–0.035 rad/m), plus a "hairpin" band generator at pressure > 0.6 |
| Elevation | Low-frequency noise (0.06–0.35 m/m), plus authored ramps and a "descent to a river" set piece |
| Width | 18 m → 8 m as a function of pressure, modulated by noise ±2.5 m, with pinch points at authored chokepoints |
| Banking | Auto-computed from curvature (`bank = clamp(k · v_ref² / g, 0, 9°)`) plus authored banking on hairpins |
| Elevation profile per cell | Each 40–70 m cell carries a "mood": open, tight, fast, technical, ambush |

### 3.1b The bank is the boundary

The trail is **cut below** the surrounding ground and the bank rises steeply
just past the trail edge (roughly 40–50° of loose earth and roots). The
corridor is therefore held by the terrain, not by an invisible wall: the player
can see exactly where the line ends, and the penalty for crossing it is
immediate loss of speed and a recovery back onto the trail.

### 3.2 What replaces lanes

Instead of three lanes, the corridor contains a continuous arrangement of
**hazards and affordances**:

- **Boulder fields** — clusters of 0.6–2.2 m rocks; you must pick a line, and the line is never marked.
- **Root shelves** — raised root ridges crossing the trail, launching the car and unloading it in the air (bad, because the canopy is right there).
- **Mud shelves** — deep wet sections that bog the engine; the player *should* slow down, and slowing down is exactly when the canopy is strongest. This is the game's central risk/reward tension, expressed in geometry rather than in UI.
- **Fallen logs** — rotten capsules lying across the trail: low enough to ride
  over, light enough (20–90 kg) to shove aside with the bumper. Momentum is
  shared, so a log costs you speed but never ends the run.
- **Chokepoints** — two boulders 3.2 m apart, forcing precision.
- **Archways** — low tree limbs you can pass under (clear of the canopy) or crash into.
- **Creek crossings** — water, with a fast gravel bar as the racing line.

The **racing line is emergent**: the fastest line is the one between the rocks
and over the root shelf, and it changes constantly. Players develop line
knowledge, not lane reflexes.

### 3.3 Canopy cells

The corridor is divided into **canopy cells** of 40–70 m. Each cell has:

| Property | Meaning |
|---|---|
| `density` | Nodes available (18–46), scaled by pressure |
| `closure` | How low the canopy hangs (4.5 m → 2.8 m) — controls whether you can drive under a low limb |
| `moisture` | 0–1, drives germination and vine slickness |
| `lightLevel` | Drives dappled light intensity, and the player's ability to read a tip |
| `profile` | Composition of the cell: open sky / liana curtain / boulder shelf / flood |

Cells are generated ahead of the player (a 300 m lookahead), consumed behind, and
freed. The player can *see* a dense cell coming — that is the long-range read
that makes the difficulty ramp legible rather than unfair.

### 3.4 Track query API

Both the simulation and the renderer read the world through one interface, so
what you see is exactly what you hit:

```ts
interface TrackQuery {
  nearest(p: Vec3): { s: number, lateral: number, height: number,
                       surface: SurfaceId, curvature: number,
                       bank: number, halfWidth: number };
  sampleSurface(s: number, lateral: number): { height, normal, surface, grip };
  canopyCellAt(s: number): CanopyCell;
  propsAlong(s0: number, s1: number): PropInstance[];
}
```

---

## 4. Props and collision

| Prop | Collision | Mass | Can be scraped (vine damage) |
|---|---|---|---|
| Boulder | Convex hull, static | ∞ | ✔ (huge, causes 3.5 kN·m impulse) |
| Tree trunk | Cylinder, static | ∞ | ✔ (best anchor breaker) |
| Fallen log (rotten) | Capsule, dynamic | 20–90 kg | ✔ (shoved aside with momentum transfer) |
| Root shelf | Heightfield feature, static | ∞ | — |
| Rock (small) | Sphere, dynamic | 40–300 kg | ✔ (can be knocked away, and it *is* a boulder for the scrape) |
| Log (small) | Capsule, dynamic | 20–80 kg | ✔ |
| Boulder field | Cluster of static spheres | ∞ | ✔ — this is the wall you have to pick a line through |
| Chokepoint | Two static spheres, 3.2 m gap | ∞ | ✔ |
| Fern / bush | None (soft) | — | — (decorative only, but vines spawn from them) |

Dynamic props are simulated as rigid bodies with their own mass and friction,
so a knocked rock tumbles realistically and can be used as a weapon.

---

## 4b. Recovery rules (implemented)

A run never dead-ends. Four safety nets, all of them diegetic enough to hide
behind a "marshal" fiction:

| Situation | Rule | Feedback |
|---|---|---|
| Car on its roof | Recovered after 2.5 s | Camera settles, latches let go |
| Wedged on stone with the throttle open | Recovered after 4.5 s onto the clearest line | "BACK ON TRAIL" toast |
| Chassis buried in the ground | Recovered the moment the body is 0.35 m under the terrain | Dust and shake |
| More than 1.5 m outside the trail edge | Recovered after 1.0 s | Same toast, plus the player is put back on the racing line |

Recovery never resets distance, speed or the canopy pressure: it costs the
player their line, not their run.

## 5. Feel targets & test metrics

These are the numbers that decide whether the driving is "AAA" or "toy". They
are measured on a fixed procedural test loop (`Track_QA_A`, 1.2 km, published
with the build) and asserted in CI.

| Metric | Target | Acceptable |
|---|---|---|
| 0–100 km/h | 6.4 s (dry dirt, 4WD) | 5.6 – 7.8 s |
| Top speed (flat, 1.2 km) | 158 km/h | ≥ 145 km/h |
| 100–0 km/h braking | 42 m | 36 – 52 m |
| Lateral g (packed dirt) | 1.05 g | 0.9 – 1.2 g |
| Slide angle at limit | 8 – 14° | ≤ 18° |
| Steering response (input → yaw rate 90 %) | 180 ms | 140 – 260 ms |
| Suspension bottom-out rate | < 2 % of landings | — |
| Camera FOV at top speed | 78° → 92° | monotone, no snap |
| Time to full lock | 210 ms | 160 – 300 ms |

**No arcade aids are on by default.** No traction control, no stability
management, no auto-brake, no drift-assist, no rubber-banding. The assist menu
exists but every item is off at launch, and turning on an assist *tells the
player it is on* in the pause menu. The game respects the fantasy: you are
driving, and the jungle is pulling on you.

---

## 6. Collision between vines and the world

Vines are also affected by the world — a vine that whips into a rock should
wrap it. Simplified:

- Each vine node is tested against the track query's prop list (broadphase by
  `s` bucket).
- A node that penetrates a prop is pushed out and becomes a temporary fixed
  anchor for that vine (with reduced strength) — this is how a vine ends up
  wrapped around a boulder and how a scrape can occur naturally when the car
  pushes a latched vine into a tree.
- Node-node collision between different vines is disabled (too expensive, not
  visible); node-self-collision uses a simple distance constraint along the
  polyline only.
