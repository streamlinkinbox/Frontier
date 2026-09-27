# Measuring & improving the animation quality

This note explains the numbers used to judge the pedipalp / leg / attack fixes,
how to reproduce them, what is still imperfect, and where to push next.

All distances are in **game units = centimetres** (the spider has a ~13 cm leg span).

---

## 1. What the pedipalp table means

This is the table from the pedipalp fix (commit `d7cc439`):

| Metric | Before | After |
|---|---|---|
| Frames with a palp segment sunk **> 3 mm** into rock | 1816 | 308 (−83 %) |
| Frames with a palp segment sunk **> 1 mm** into rock | 3898 | 924 |
| Frames with a planted palp **cramped under the head** | 4946 | 765 |
| Threat-pose glitch | yes | gone |

**How it was sampled.** The spider walks around the cave on autopilot (the same AI
wander that `?mode=ai` uses). It runs for **4 different random seeds × 60 s at 60 fps
= 14 400 frames**. Every frame is checked. A count of 308 means the problem was visible
in 308 of the 14 400 frames (~2 %). It does *not* mean 308 separate incidents: one
incident usually lasts several consecutive frames.

**Metric definitions** (implemented in `tools/probes/walk.js`):

| Name in probe output | Meaning |
|---|---|
| `palp03` | The frame counts if *any* palp joint from the patella to the tip is more than **0.3 cm** inside rock. Depth comes from the signed distance `world.closest()`, which is negative inside rock. This is the "you can see it clip" threshold at gameplay camera distance. |
| `palp01` | Same test with a **0.1 cm** threshold. Very strict: at that depth the setae hide it, so this is mostly a trend indicator. |
| `palpWorst` | The deepest palp penetration seen in the whole run, in cm. |
| `palpCramped` | A *planted* (non-swinging) palp tip is closer to its socket than **0.45 × maxReach**. That is when the palp folds up under the chelicerae and looks crushed and sunk into the ground. |
| `nan` | Frames where any joint position is NaN. This caused the old threat-pose glitch, where the palps flashed to the origin while rearing up. Must stay 0. |
| `leg03`, `leg1`, `legWorst` | The same penetration test for the 8 walking legs (femur to tarsus tip), at 0.3 cm and 1 cm thresholds, plus the deepest value. |
| `femMax` | The largest femur-lift angle used by the palp IK clamp. It is a sanity check that the clamp is not saturating. |

### Current baseline (this commit)

| Metric | cd0672e | d7cc439 | now |
|---|---|---|---|
| palp03 | 1816 | 308 | 283 |
| palp01 | 3898 | 924 | 1070 |
| palpWorst | −1.39 | −0.99 | −1.06 |
| palpCramped | 4946 | 765 | 778 |
| nan | >0 | 0 | 0 |
| leg03 | 734 | 712 | 765 |
| leg1 | 143 | 98 | 140 |
| legWorst | −1.45 | −2.43 | −2.14 |

**Why the numbers move a little between versions even when walking code is unchanged.**
The AI occasionally strikes or turns in response to what it sees. Any change to an
action (e.g. the new strike landing) nudges the spider onto a slightly different path
through the cave from that moment on, and different rocks give different counts.
Differences of roughly ±15 % on `palp01`/`leg1` are path noise. For this commit I split
the leg counts by "within 1.5 s of a strike" versus "otherwise". The strike contributed
**0** of the 140 `leg1` frames; all of them came from ordinary walking on a different
route. Always compare on **all 4 seeds**. A single seed can be off by 2×.

---

## 2. Attack-animation audits (strike & hair-flick)

`tools/probes/strike.js` triggers a strike at 3 locations and samples every frame.
`tools/probes/flick.js` does the same for the urticating-hair flick.

**Strike (E / Space), legs I–II.** Before this change, the front legs were driven to
authored points too close to their sockets. They folded into a "crumpled" Z-shape and
were pushed into the floor on landing.

| Metric | Before | After |
|---|---|---|
| `minFold`: smallest tip-to-socket distance / maxReach (lower = more crumpled) | 0.32 | 0.73 |
| Frames with a front leg folded below 0.45 reach | 78 | 0 |
| Frames with a front leg inside rock | 40 | 0 |
| Deepest rock penetration | −0.86 cm | 0 |
| Largest per-frame jump of a foot (`popMax`) | 3.71 cm | 0.98 cm |

The strike is now a sequence of three phases:

1. **Wind-up.** Legs I–II blend into the raised threat pose.
2. **Slam.** Each foot comes down onto its own foothold ~1.6–1.9 cm ahead of its rest
   spot. The foothold is found by the same collision-checked planner the gait uses
   (`_planFoothold`) and cached once per strike.
3. **Hand-over.** At 33 % of the strike the foot's contact point *becomes* that
   foothold, so the regular walking IK takes over with no pop.

**Hair flick (R), leg IV.** This went through two revisions.

1. The first rewrite stopped the leg passing through the abdomen's body. It still rubbed
   across the *top* of the abdomen, inside the long setae pile, so on screen the legs lay
   over the abdomen.
2. The current version keeps every leg segment beside the abdomen.

The key fact is that a tarantula leg flexes in one vertical plane, so seen from above
the whole leg is the straight line from its coxa to its tarsus. The tarsus therefore
travels along the line from the coxa that is **tangent to the abdomen's hairy outline**.
It passes the widest flank, then kicks backward and outward off the abdomen, releasing
hairs. The legs alternate half a cycle apart. The outline, including the setae, was
measured from the real geometry with `tools/probes/abdsil.js` and is stored as
`ABD_*` in `Actions.js`. The tangent is recomputed every frame from the actual coxa
position, so it follows the abdomen's tilt during the flick.

| Metric (81 frames of active flicking) | Original | 1st rewrite | Now |
|---|---|---|---|
| Frames with a leg-IV segment inside the abdomen's hair envelope | 81 | 81 | **0** |
| Frames with a leg-IV segment over the abdomen, seen from above | 81 | 81 | **0** |
| Worst overlap from above | — | 1.49 cm | **0** |
| Frames in rock | 0 | 0 | 0 |
| Tightest leg fold (tip distance / maxReach) | — | 0.43 | 0.43 |

The only remaining contact is during the 0.3 s blend from the standing pose into the
flick and back: 28 of 150 frames, at most 2 mm. In the normal standing pose, the base
of femur IV already sits against the front corner of the abdomen, as in the real
animal.

---

## 3. How to run the measurements

The tools run the real game in headless Chromium with the render loop driven manually
(`?manual`). The probes are deterministic for a given build.

```bash
# one-time setup
cd tarantula && npm i
cd tools && npm i            # puppeteer-core + @sparticuz/chromium
# @sparticuz/chromium needs a few shared libs; extract them once into tools/libs/lib
# (the tarball ships inside node_modules/@sparticuz/chromium/bin/*.tar.br)

# terminal 1
cd tarantula && npm run dev   # serves on :5173

# terminal 2
cd tarantula/tools
export LD_LIBRARY_PATH=$PWD/libs/lib
node run.mjs "http://localhost:5173/?q=low&manual&nohud" out/walk  probes/walk.js
node run.mjs "http://localhost:5173/?q=low&manual&nohud" out/idle  probes/idle.js
node run.mjs "http://localhost:5173/?q=low&manual&nohud" out/strk  probes/strike.js
node run.mjs "http://localhost:5173/?q=low&manual&nohud" out/flk   probes/flick.js
```

`run.mjs <url> <outPrefix> <file>`:
- a **`.js`** file is evaluated once in the page and its return value is printed;
- a **`.json`** file is a list of steps `{ "eval": "..." }`, `{ "wait": ms }`,
  `{ "shot": "name" }` for scripted screenshots. Use `q=high` for beauty shots.
  They take 1–4 minutes each.

Probes:

| Probe | What it checks | Runtime |
|---|---|---|
| `walk.js` | Everything in section 1 | ~30 s |
| `idle.js` | Parks the spider at 5 spots (floor, slopes, walls) for 30 s each and counts foot steps. It should be **0**; idle must not "tap dance". | ~20 s |
| `strike.js` | Section 2, strike table | ~10 s |
| `flick.js` | Section 2, flick table | ~10 s |
| `abdsil.js` | Measures the abdomen outline including setae (per z-slice), feeding the `ABD_*` table in `Actions.js`. Re-run it if the abdomen or its hair changes. | ~10 s |
| `camlib.js` | Helper `__cam(dist, elev, az, targetUp)` for placing the camera in screenshot scripts. `az` 0 = front, +π/2 = right side. | — |

`window.__game` exposes `{ THREE, spider, world, camera, step(sec, fps), render(), freezeCam, brain, cave }`
for writing new probes.

**Workflow for a change:** run `walk.js` before and after on the same build settings.
Accept only if `palp03`, `leg1` and `nan` don't get worse outside the ±15 % path noise,
and `idle.js` stays 0 steps.

---

## 4. What is still imperfect, and how to improve it

Ordered by visual impact.

### 4.1 Palp contact when the face is pushed against a rock face (~2 % of frames, worst ≈ 1 cm)
Nearly all remaining `palp03` frames happen when the spider walks head-first into a
wall or overhang. The palp tips have nowhere to go, so the patella (joint 3) dips into
the rock at low reach. The tarsus (joint 5) also clips during its swing.

Ideas:
- **Palp retract state** (`Tarantula._palpFoothold`). When no foothold with reach
  ≥ 0.6 exists within the forward cone, stop planting. Lift the palps into a raised
  "feeling" pose (like the threat pose, but lower) and tap the wall surface with the
  tarsus instead. Real tarantulas do this constantly. It would remove most of the
  cramped frames too.
- **Tilted bend plane for the palps** (`Limb.js`, `planeDir` / `_solveChain`). Every
  distal joint flexes inside a plane that is vertical in body space; only its yaw
  changes. Adding a roll so the plane tilts outward by up to ~35° when reach < 0.55
  lets the patella clear the rock sideways instead of pushing down into it.
- **Swing arc from the SDF** (palp swing lift in `Tarantula._gait`). The swing lift is
  capped at 0.4 cm, because larger values looked like waving. Instead of a fixed
  parabola, sample `world.closest()` at 3 points along the chord and raise only where
  needed.

### 4.2 Leg segments grazing rock (`leg1` ≈ 1 % of frames, worst ≈ 2 cm)
These happen almost exclusively on sharp concave creases (floor-to-wall transitions),
where the femur–patella knee sits above the crease and the tibia passes through the
corner.

Ideas:
- `Tarantula._legCollision` already samples 5 points per leg (knee, patella/tibia and
  metatarsus midpoints, tarsus) via `_legPenetration`. When one collides, it tries 8
  alternative (femur-lift, tarsus-bias) configurations and eases toward the best one.
  The leftovers are cases where **none** of the 8 candidates is clear. Two options:
  1. Add a second, wider ring of candidates, only evaluated when the first ring fails,
     so the cost stays low.
  2. Let the planted foot slide a few mm along the surface toward the socket (reducing
     reach) as a last resort.
- `_planFoothold` could penalise footholds where the straight line from the socket to
  the foothold passes through rock (3 SDF samples along the segment). The gait would
  then pick a spot on the wall itself instead of deep in the crease. That prevents the
  bad pose from happening at all, rather than repairing it afterwards.
- The body-clearance sample points (`this._clearPts`, used by `_clearance()`) cover the
  chelicerae, the underside of the prosoma, the palp bases and the abdomen. Adding points
  under the leg coxae would lift the body slightly on creases, which opens the leg angles.

### 4.3 Attacks
- The strike's slam footholds are computed once at wind-up. If the target moves, the
  legs land where it *was*. For a gameplay-driven strike, re-plan at t = 0.2 toward the
  prey position (see `sw.act.slam` in `Actions._override`).
- The fangs and chelicerae unfold on a fixed curve. Driving them from the lunge
  distance (a bigger lunge gives a wider gape) would sell the impact more.
- The flick emits hairs from the tarsus on every back-stroke. For extra realism, emit
  from the abdomen patch the tarsus just scraped (nearest point on the ellipsoid) and
  make the patch slowly go bald. *B. hamorii* keepers recognise that immediately.

### 4.4 General
- Everything is tuned against `q=low` physics, but the IK is the same at every quality
  level. Visual fixes should still be checked with a `q=high` screenshot, because hair
  cards hide small clips at low quality.
- Add new failure modes as probe counters **before** fixing them. Every fix in this
  repo started as a number going down.

### Code map

| Concern | Where |
|---|---|
| Walking foot placement | `src/spider/Tarantula.js` → `_foothold`, `_planFoothold`, `_gait` |
| Palp foot placement & swing | `Tarantula._palpFoothold`, `_gait` (palp branch) |
| Body height / clearance | `Tarantula._clearance` (sample points `_clearPts`), `_adhere` |
| Leg–rock collision | `Tarantula._legPenetration` / `_legCollision`, `Actions._groundGuard` (authored action poses) |
| IK (legs & palps) | `src/spider/Limb.js` (palp angle clamp `[−0.5, 1.95]` + femur lift) |
| Threat / strike / flick | `src/spider/Actions.js` (`THREAT` poses, `_override`) |
| Rock queries | `src/world/SurfaceWorld.js`: `raycast()` hits outward faces only; `closest()` returns a signed distance, negative inside rock. Use `closest()` for any point that might start inside rock. |
