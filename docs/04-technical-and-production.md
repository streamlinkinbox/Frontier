# 04 — Technical Architecture & Production

---

## 1. Module map

```
                       ┌────────────────────────────────────┐
   TrackGenerator ───► │  WorldStream (cells, s-bucketed)   │
                       │  · canopy cells                    │
                       │  · props (static/dynamic)          │
                       │  · surface fields                  │
                       └───────────────┬────────────────────┘
                                       │ TrackQuery
             ┌─────────────────────────┼─────────────────────────┐
             ▼                         ▼                         ▼
      ┌─────────────┐         ┌───────────────┐         ┌──────────────┐
      │ VehicleSim  │◄───────►│  VineSim      │◄───────►│  Director    │
      │ 6-DOF body  │ tension │  growth state │  spawn  │  pressure /  │
      │ tyre model  │ impulses│  constraints  │  orders │  staging     │
      │ suspension  │────────►│  creep/slip   │         └──────────────┘
      └──────┬──────┘  contact└───────┬───────┘
             │                         │
             ▼                         ▼
      ┌────────────────────────────────────┐
      │  Presentation (interp + FX)        │
      │  camera · audio · vfx · hud        │
      └────────────────────────────────────┘
```

### 1.1 Module responsibilities

| Module | Owns | Must not own |
|---|---|---|
| `TrackGenerator` | Spline, surfaces, canopy cells, prop spawns | Any runtime state |
| `WorldStream` | Cell lifetime, streaming, spatial buckets | Simulation logic |
| `VehicleSim` | Rigid body, tyre model, suspension, powertrain | Rendering, input mapping details |
| `VineSim` | Vine lifecycle, growth, geometry, constraints | Difficulty decisions |
| `Director` | Pressure curve, spawn staging, guarantees | Physics |
| `Presentation` | Camera, audio, VFX, HUD | Simulation state mutation |

**Rule:** `VineSim` never reads the difficulty curve directly, and `Director`
never touches physics. They communicate through the spawn orders and the
pressure value. This separation is what makes the mechanic tunable in isolation.

---

## 2. Threading & frame flow

| Thread | Work | Budget (PS5 / Series X) |
|---|---|---|
| Main | Presentation, input, director, cell streaming | 2.0 ms |
| Sim (240 Hz) | Vehicle + vine integration | 2.6 ms |
| Job system | Prop rigid bodies, particle sim, tree LOD | 1.4 ms |
| GPU | Rendering + compute (vine growth assist, particles, culling) | 10.5 ms |

**Frame order:**
1. Input sample → smoothed action values.
2. Director update (spawn orders, pressure).
3. Fixed-step simulation loop (240 Hz, accumulator, ≤ 8 substeps):
   a. Vehicle: forces from suspension + tyres + aero + **vine constraints**.
   b. Vines: growth, node integration, constraint solve, creep, break checks.
   c. Integration + interpolation state capture.
4. Presentation: camera, audio, VFX, HUD.
5. Render.

**Determinism.** The sim is deterministic given (seed, input stream). Replays
store the seed + 30 Hz input snapshots (≈ 1.2 KB/s of run) — cheap enough to
ship, and it lets the QA team reproduce any bug exactly.

---

## 3. Key data structures

```ts
type SpeciesId = 'tendril' | 'anchor' | 'snare' | 'claw' | 'dragline' | 'liana';

interface VineSpec {            // immutable, per species
  massPerMetre: number;
  stiffness: number;
  damping: number;
  maxTension: number;
  gripMu: number;
  extensionSpeed: number;
  leadTime: number;
  breakMode: 'snap' | 'shear' | 'release';
}

interface VineNode {             // one per polyline point
  pos: Vec3; prev: Vec3; vel: Vec3;
  mass: number;
  radius: number;
  pinned: boolean;              // true for the root node
  propAnchor: number;           // -1, or index of a prop it is snagged on
}

interface Vine {
  id: number;
  species: SpeciesId;
  spec: VineSpec;
  state: VineState;
  nodes: VineNode[];
  curve: Curve;                 // polyline + cached frames for rendering
  latch: Latch | null;          // active adhesion constraint
  growth: number;               // 0..1 along the stem
  target: Vec3;
  age: number;
  tension: number;              // smoothed, drives audio/vfx
}

interface Latch {
  proxyId: number;              // which body proxy was hit
  anchorLocal: Vec3;            // contact point in body space
  restLength: number;
  adhesion: number;             // 0..1 ramp
  mode: 'grip' | 'suction' | 'wrap';
  creep: number;
  extraContacts: LatchPoint[];  // wrap mode
}
```

### 3.1 Constraint solve (per step, per latch)

```
worldPoint = bodyTransform(anchorLocal)
r          = worldPoint - vineTip.pos
len        = length(r)
dir        = r / len
stretch    = len - restLength
relVel     = dot(bodyPointVelocity - vineTip.vel, dir)
T          = max(0, k*stretch + c*relVel) * adhesion
T          = min(T, maxTension * 1.2)          // over-stretch is clamped, not explosive

// decompose into the body frame
bodyDir    = inverseRotate(bodyQuaternion, dir)
F          = bodyDir * T

// apply at the contact point
applyForceAtPoint(body, F, worldPoint)
applyForce(vineTip, -F)

// friction / slip
tangent     = dir - bodyDir*dot(dir, bodyDir)    (in world)
tangentMag  = length(tangent)
if tangentMag > 1e-5:
    tDir       = tangent / tangentMag
    shear      = clamp(springShear * slipAccum, -μ*T, μ*T)
    applyForceAtPoint(body, -tDir*shear, worldPoint)
    if |slipAccum| > slipThreshold:
        anchorLocal += inverseRotate(bodyQuat, tDir) * slipAccum * dt
        // creep: if adhesion > 0 and the vine is being pulled, migrate inward
        if tension > 0.5 * maxTension:
            anchorLocal = lerp(anchorLocal, creepTarget, creepRate * dt)
```

The body receives equal-and-opposite impulses, so **the vine genuinely pulls on
the car** — the same solver path as the tyres, no special-casing.

### 3.2 Growth update (per step, per vine)

```
switch state:
  DORMANT:    if germinationScore(node, player) > 1 → GERMINATING
  GERMINATING: growth += dt / germinateTime;  if growth >= 1 → EXTENDING
  EXTENDING:  target = predictPlayer(leadTime)
               tipTarget = lastNode.pos + normalize(target - lastNode.pos) * extensionSpeed * dt
               insert node when the tip has travelled > nodeSpacing
               if length(target) < 0.6 or growth > maxGrowth → MATURE
  MATURE:     tip drifts with wind noise; re-targets the player if within reacquireRadius
  LATCHING:   adhesion = smoothstep(t / 0.15); if tip separates → WITHERED
  LATCHED:    creep += creepRate * dt;  contact points migrate
  CREEPING:   as above, plus increased visual stretch
  BREAKING:   0.05 s; then BROKEN → WITHERED
```

**GPU assist.** For the *unlatched* growth (which is the vast majority of
vine work), the extension target and node insertion can run in a compute shader
over a texture of vine nodes, with the CPU reading back only the tip positions
of the ~20 vines near the player. This removes ~1.8 ms of CPU work. Latched
vines always run on the CPU because they need exact constraint interaction.

---

## 4. Engine notes

The systems are engine-agnostic (pure data + math). Mapping:

| System | Unreal (Chaos) | Unity (PhysX) | Custom |
|---|---|---|---|
| Vehicle body | Chaos rigid body, custom force integration preferred (full control of tyre model) | Rigidbody + custom `FixedUpdate` solver | Custom 6-DOF |
| Tyre model | Custom `Chaos::FTyre` or applied manually | Manual in `FixedUpdate` | Manual |
| Vine constraints | Chaos `FConstraint` (custom, or `FRigidBodyConstraint` with drive) | `ConfigurableJoint` per latch (max 128) | Custom impulse solver |
| Vine geometry | Procedural mesh component / Niagara | `Mesh` + `NativeArray` | Custom |
| Growth compute | Custom shader + `FRenderResource` | Compute shader + `GraphicsBuffer` | Compute shader |

**Recommendation:** custom tyre + custom vine solver in both cases. The vine
constraint must be integrated in the same accumulator as the tyres to feel
right, and off-the-shelf joint solvers add too much compliance for a game where
"the vine should feel like a wire, not a spring".

---

## 5. Performance budget (1080p / 60 fps, Series X, quality "Canopy")

| Item | Budget |
|---|---|
| Sim thread (240 Hz vehicle + vines) | ≤ 2.6 ms/frame |
| Render: opaque | ≤ 5.5 ms |
| Render: foliage/canopy (alpha-tested, clustered) | ≤ 2.2 ms |
| Render: vines (≤ 60 LOD0 + 160 LOD1/2) | ≤ 1.4 ms |
| Render: shadows (3 cascades) | ≤ 1.6 ms |
| Post (god rays, tonemap, vignette, motion blur) | ≤ 1.1 ms |
| VFX (particles) | ≤ 0.9 ms |
| **Total GPU** | **≤ 13.5 ms** |
| **Total CPU** | **≤ 5.5 ms** |

**Memory.** World cells: 3.2 MB per 60 m cell → 24 MB live. Vines: 220 ×
14 KB = 3.1 MB. Textures: vine atlas 4 MB, canopy 12 MB, road 8 MB.

**Scalability tiers.** "Canopy" (PS5/Series X), "Understory" (PS4/Xbox One,
no volumetrics, 2 cascades, vines LOD0 ≤ 30), "Fern" (Switch, 900p, no
shadows on foliage, vines billboard-only). The **mechanic is identical in all
tiers** — only representation changes.

---

## 6. Tooling & QA

| Tool | Purpose |
|---|---|
| **Track baker** | Exports the procedural corridor to a `.ftrack` for design review in an in-game fly camera |
| **Vine lab** | A test scene with a stationary car and a controlled single vine; sliders for every species parameter, live tension graph |
| **Bot racer** | An AI driver (pure-pursuit + speed map) that drives the same physics; used for regression and for "ghost" runs |
| **Tension graph** | Dev overlay plotting per-latch tension vs. time — the single most useful debug view for this mechanic |
| **Determinism harness** | Replays a seed+input and asserts a bit-identical sim state |
| **Cinematic capture** | Automated runs with a rail camera for trailers and bug reports |

---

## 7. Production roadmap

| Milestone | Weeks | Deliverable |
|---|---|---|
| **M0 — Spike** | 3 | Grey-box: flat corridor, one box car, one vine that latches and pulls. Validates the feel of the constraint. |
| **M1 — Vehicle** | 6 | Full tyre/suspension/powertrain model; QA track; feel metrics pass. |
| **M2 — Corridor** | 5 | Procedural generator, surfaces, props, canopy cells, streaming. |
| **M3 — Vines** | 7 | Full lifecycle, 6 species, constraints, creep, break, cutter counterplay. |
| **M4 — Presentation** | 6 | Vine art, canopy, lighting, VFX, camera, audio, HUD. |
| **M5 — Director & content** | 4 | Pressure curve, staging, species mix, biome authoring. |
| **M6 — Content & polish** | 5 | 3 biomes, 12 min run length, tuning pass, accessibility pass. |
| **M7 — Cert & launch** | 4 | Perf on all tiers, localisation, accessibility audit, cert, patch. |

**Total: 40 weeks.** The risk is in M3 (the mechanic), which is why M0 exists and
is scheduled first.

---

## 8. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| The vine pull feels "cheap" rather than physical | Med | **High** | Constraint must live in the same solver as the tyres; contact at real body points; no scripted yaw. M0 tests exactly this. |
| Players cannot tell a latched vine from a passing vine | Med | High | Three-channel feedback rule (§5 of overview); contact flare; audio duck; camera shake. |
| The canopy becomes visual noise | High | Med | Density budget per cell; vines get dedicated silhouette treatment; the corridor stays open (≥ 1.8 s clear line guarantee). |
| Reading the road is impossible at speed | Med | High | Surface transitions are albedo-contrasted, not hue-only; the director never spawns a vine in the same 15 m as a blind corner. |
| Performance on the base tier | Med | Med | GPU growth assist; aggressive vine LOD; scalability tiers are art-complete, not feature-stripped. |
| Players expect the vine to be dodgeable at all times | High | Med | Document it: the vine aims at a *predicted* position, so it is dodgeable by *changing* your speed/line, not by standing still. Teach this in the first 200 m. |

---

## 9. Accessibility & options

| Option | Default | Notes |
|---|---|---|
| Vine density | 100 % | Scale spawn counts 50–150 %; the mechanic stays, the volume changes |
| Reduced camera shake | auto (from OS) | 0–100 % |
| Camera roll on turns | 50 % | 0 = locked horizon |
| Motion blur | 50 % | Off for motion sensitivity |
| Colourblind mode | off | Adds a pattern to the tension tell (the vine's vibration gets a stripe) |
| High-contrast vine tips | off | Brightens the tip and the contact flare |
| Audio duck on latch | on | Can be turned off for players who find it disorienting |
| Vibration | 100 % | 0–100 % |
| Subtitles for audio cues | on | "Rustle overhead", "Vine impact" — optional |
| Steering assist | off | Never on by default; explicitly labelled |
| Left-handed UI | auto | |

**Colour is never the only channel.** Every gameplay tell is also an audio cue,
a haptic cue, and a motion cue.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Canopy cell** | A 40–70 m segment of forest with its own density, closure, and vine budget |
| **Canopy Pressure** | The global difficulty dial, 0 → 1, driving density, aggression, and corridor width |
| **Liaison** | The load-bearing stem of a vine |
| **Node** | A discrete mass point along a vine; also the weakness where an anchor shears |
| **Adhesion constraint** | The coupling between a vine tip and a body proxy |
| **Creep** | Slow migration of the contact point toward the vehicle's centre, increasing leverage |
| **Slip** | Tangential sliding of a contact when friction capacity is exceeded |
| **Director** | The system that stages vine spawns and guarantees readability |
| **Tell** | The window between a vine becoming visible and it being able to contact the player |
| **Corridor** | The continuous, variable-width natural path the player drives |
| **Scraping** | Using a tree or boulder to break a latched vine |
