# Frontier

## Research-driven female *Aedes aegypti* — real-time 3D hero rig

A mosquito built to a brief, not to an art style: every proportion, joint
limit, gait parameter and wing-beat number in `docs/ANATOMY.md` is traced to
published morphometry and behaviour, and the simulation is held to hard
invariants by a headless suite.

```bash
npm install
npm run dev        # http://localhost:5173
npm run build      # → dist/
node tools/verify.mjs
node tools/shots.mjs   # CPU-rasterised PNGs, for visual review
```

Scene unit = 1 mm. No runtime CDN: Three.js is a pinned npm dependency.

### What it does

- **Walks** on any analytic surface — terrain, deck, a curved fuel-tank hull,
  a vertical wall, plate glass — switching attachment the way a real insect
  does, with stance feet that are world-locked to **exactly zero slip** and
  never fewer than three feet down.
- **Never crosses its own legs.** The tarsi are held outboard of the body
  midline by a hard constraint in the IK, not by tuning, and the rendered
  pretarsus is what the test measures — not the planner's intent.
- **Flies** at 532 Hz with a 44° stroke and a 6.7% reversal duty, measured
  against the wingbeat literature rather than eyeballed.
- **Feeds.** Probe → salivate → engorge → withdraw, the labium sheath folding
  into a real loop, the abdomen engorging and going translucent.
- **Exports a rigged GLB** with three baked animation clips.

### Layout

| Path | |
|---|---|
| `docs/ANATOMY.md` | the character bible — every constant and its source |
| `src/mosquito/anatomy.js` | the numbers; everything else consumes them |
| `src/mosquito/build.js` | the articulated rig and all procedural geometry |
| `src/anim/legs.js` | analytic IK, joint limits, tarsal chain, pretarsus |
| `src/anim/gait.js` | gait ladder, foothold planning, surface negotiation |
| `src/anim/flight.js` `feeding.js` | flight kinematics, the blood-meal state machine |
| `src/world/` | analytic surface field and the depot scene |
| `src/export/bake.js` | GLB animation baking |
| `tools/verify.mjs` | 103 headless checks on the simulation invariants |
| `tools/raster.mjs` `shots.mjs` | CPU rasteriser for visual review without a GPU |

### Invariants the suite enforces

Zero stance slip · ≥ 3 supporting feet at all times · no rendered foot crosses
the midline or another foot · femur–tibia flexion inside 6°–132° · gait phase
offsets match the published tripod tables · stroke reversal ≈ 6% of the cycle ·
a real labium loop · every baked track well-formed and resolvable · the fuel
vent is a genuinely standable surface · no NaN across 10 000 frames of mixed
surface transitions.

---

## Vector Lab — original interactive mosquito lab (`mosquito/`)

The first-generation specimen rig from this project: a fully procedural
female *Aedes aegypti* (lyre scutum, pilose antennae, banded tarsi, scaled
wings + fringe, halteres, 8-segment abdomen, claws + pulvilli) with
wave→tetrapod→tripod gait, 40°/~560 Hz flight, wall + ceiling walking, and
a fuel-tank / blood-dish feeding sequence. Zero-build static page —
Three.js r160 is vendored, so serve the folder over HTTP and open it:

```bash
python3 -m http.server --directory mosquito 8123   # → http://localhost:8123
```

Kept live alongside the hardened `src/` rig above because it remains the
quickest way to review the mosquito's behavior set in a browser.

## Tick Lab — giant battery tick (`tick/`)

Standalone second specimen: two giant female hard ticks that quest at the
trackside, detect the idling race car (CO₂ + heat), climb the rear bumper,
breach through the side air duct, and cement onto the battery cells —
draining the pack until the lights die, then dropping off replete.
Alternating-tetrapod gait, barbed hypostome + cement cone, scutum that never
expands while the alloscutum balloons. Serve `tick/` over HTTP:

```bash
python3 -m http.server --directory tick 8124       # → http://localhost:8124
```
