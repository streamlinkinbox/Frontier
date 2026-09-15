# Frontier — T-REX // Biomech Unit 🦖

An **anatomically-informed *Tyrannosaurus rex* skeleton** (units in meters,
~10 m long) with **functional hydraulic pistons** driving its legs, jaw and
arms, a **verlet-simulated tail**, and an **IK-driven walk-in-place** gait on
a treadmill deck. Built procedurally with three.js — no model files, and
**no internet required** (three.js r160 is vendored in `js/vendor/`).

Anatomy follows published *T. rex* material: 10 cervicals, 13 dorsals,
5 fused sacrals, 40+ caudals, 18–19 gastralia pairs, antorbital fenestra,
quadrate–articular jaw hinge, 2-fingered manus, arctometatarsalian pes
([Wikipedia — Tyrannosaurus](https://en.wikipedia.org/wiki/Tyrannosaurus)).

Measured 1:1 reference values used in the build (Sue FMNH PR2081 / Hartman
skeletal USNM 555000, ~11.6 m), scaled here to a ~10 m adult:

| Element | Length |
|---|---|
| Skull | 1.46 m, true antorbital / orbit / temporal fenestrae |
| Femur | 1.32 m · Tibia 1.16 m · Metatarsus III 0.68 m |
| Hip height | 3.02 m · Pedal formula 3-4-5 + claws |

Key visual references: Scott Hartman's *T. rex* skeletal reconstruction
(skeletaldrawing.com) and the Tristan (MB.R.91216) skull material. All bone
geometry in `js/dino.js` is original procedural work guided by these
references — no scan data is redistributed.

> **Licensing note for game use:** the Smithsonian's *T. rex* 3D scans are
> restricted to non-commercial / educational / personal use, and most free
> online models are non-commercial or unlicensed. The procedural skeleton in
> this repo is original geometry (clean), but if you want true scan geometry
> in a commercial title, license a commercial-cleared model (or scan) and it
> can be mapped onto this exact rig — the animation, IK, tail-physics and
> hydraulic systems are geometry-agnostic.

## Run it

Serve the folder with any static file server and open the served URL
(opening `index.html` directly as a file will **not** work — browsers block
ES modules over `file://`):

```bash
python3 -m http.server 8000   # then open http://localhost:8000
```

Prefer a single file? `standalone.html` is the entire app (code + styles +
three.js) bundled into one page with zero external requests — it even works
opened directly as a file. It is built from `js/` with esbuild; rebuild after
changing sources (see `js/vendor/` note below).

## Controls

| Input | Action |
|---|---|
| `1` / `2` / `Space` | Idle / Walk / toggle |
| Camera buttons | Overview, Skull, Hindlimb, Tail close-ups |
| Click any bone | Bone inspector shows anatomical notes |
| Drag / Scroll | Orbit / Zoom |

Panel extras: belt-speed slider (0.4–2.2 m/s), gait-phase + stance
indicators, bone labels, hydraulics on/off, tail-physics on/off with
stiffness/damping sliders, ghost X-ray view, live piston-extension telemetry.

## Systems

- **`js/dino.js`** — procedural *T. rex*: researched skull (premaxilla →
  quadrate), S-curve neck with atlas/axis, barrel chest + gastralia +
  furcula, full pelvis, digitigrade hindlimbs with astragalus, 8-segment
  tail. Every mesh tagged with bone name + notes.
- **`js/gait.js`** — walk-cycle engine: 60/40 stance/swing phasing, COM
  bob/sway, pelvic roll/yaw with thoracic counter-rotation, gaze
  stabilization, foot-roll tracks (strike → pivot → toe-off), idle
  breathing/look/gape behaviors, smooth idle↔walk blending.
- **`js/ik.js`** — analytic two-bone IK (femur/tibia) with knee pole
  vector + foot/toe orientation solving. Feet stay planted during idle
  weight shifts.
- **`js/tail.js`** — verlet tail: gait-wave targets + verlet integration,
  distance constraints, ground collision, tunable stiffness/damping.
- **`js/hydro.js`** — 16 telescoping pistons re-solved every frame between
  live skeletal anchors (hip/knee/ankle/toe ×2, jaw ×2, arm ×4, tail ×2).
- **`js/main.js`** — treadmill lab, bone labels, inspector raycast, camera
  presets, dust, servo/footstep synth audio, main loop.
