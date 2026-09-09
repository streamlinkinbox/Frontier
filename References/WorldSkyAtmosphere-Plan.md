══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Sun · Sky · Atmosphere · Multi-World — refined plan
  Supersedes the planning half of TilingDiagnosis-And-SkyAtmospherePlan.md. That document keeps the tiling forensics.
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

STATUS — the prerequisite is done
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
S0 (spatial tap jitter) is COMPLETE, committed as `2ba89b5`. The reuse graph went from 121 isolated pools to 1; the residue-class
bias went from 0.296-and-never-decaying to 0.00002-and-still-falling. SPIR-V reflection is identical to HEAD across all 77 entries,
so the R6 identity proofs are untouched. 23 suites green.

Awaiting your confirmation from hardware that the lattice is gone before building on top of it.

DECISIONS TAKEN THIS SESSION
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  · **Orbital mechanics: in, but optional.** A simulated clock drives sun and moon. Optional means the orbit can be frozen or
    scrubbed, not that the code path is bypassed — one mechanism, with a rate of zero as a valid setting.
  · **The sun is a real body**, not just a direction vector: a sphere with yellowish noise, visible in the debug overview, outside
    the planet. It is a reference point for the orbit and something to *see* when you fly out.
  · **Portals: ray teleporters**, confirmed. Plus entity duplication with half-clipping at the portal plane (below).
  · **Multiplayer sync is a hard requirement** on all of it, including portals.
  · Model: **Hillaire 2020**. Rationale in the prior document; your previous parameter list is already its parameter set.


PART ONE — THE CLOCK, AND WHY IT IS THE FOUNDATION
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
Everything celestial derives from one authoritative 64-bit value: **elapsed simulation seconds**.

        SimulationTime  (Real64, seconds)
              │
              ├─▶ per world: orbital phase ─▶ sun direction ─▶ sky-view LUT ─▶ lighting
              ├─▶ per world: moon phase + position ─▶ night lighting, moon disc
              └─▶ per world: star field rotation

This is the single most important architectural decision in the plan, and it is what makes multiplayer tractable. **No celestial
state is ever replicated.** A joining client receives one number and reproduces every world's sun, moon and star field exactly.
There is no drift, no interpolation, no per-world sync messages, and no authority conflict — because there is nothing to
disagree about. It also makes the whole system trivially deterministic for replay and for the offline proofs.

Two consequences worth stating plainly:
  · Orbits must be **closed-form functions of time**, never integrated step-by-step. An integrator accumulates error and diverges
    between machines; `Position(t)` is identical everywhere, always, and is also scrubbable and reversible for free.
  · A world that is not resident still has a correct sun, because its sun is a function, not a simulated object. Costs nothing.

`WorldOrbitRecord` per body: semi-major axis, eccentricity, inclination, longitude of ascending node, argument of periapsis, mean
anomaly at epoch, orbital period. Standard Keplerian elements. Solving Kepler's equation is a handful of Newton iterations —
negligible, and done on the CPU once per world per frame, not per pixel.

**Axial rotation is separate from orbit.** Day/night comes from the planet's spin, seasons from axial tilt against the orbit. Both
closed-form. This matters for your sunset requirement: a proper sunset needs the *observer* rotating into the terminator, not the
sun swinging around a stationary observer — the two look different at the horizon.


PART TWO — THE ATMOSPHERE, PER WORLD
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
`AtmosphereConfiguration` is a plain record — your existing parameter list, unchanged. The critical structural point: it is
**owned by a world, not by the renderer**. Unreal's `SkyAtmosphere` is a level singleton, which is precisely why it cannot do what
you want. Here, N worlds each own one.

LUT residency — the cost control
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
        ① Transmittance      2D  256 × 64      per world, CONSTANT — built once, rebuilt only when parameters change
        ② Multi-scattering   2D   32 × 32      per world, CONSTANT — same
        ③ Sky-view           2D  192 × 108     per OBSERVER, per frame
        ④ Aerial perspective 3D   32 × 32 × 32 per OBSERVER, per frame

①② are tiny (~300 KB per world) and static, so every world can hold them resident regardless of distance. ③④ are per-frame work
and are only built for **worlds an observer can actually see**: the world the camera is in, plus any world visible through an open
portal. So the per-frame cost is bounded by portal count, not by world count. Ten thousand worlds cost the same per frame as two.

Sunset falls out of the physics
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
This is why the model earns its cost over an analytic fit. As the sun descends the optical path lengthens, Rayleigh scattering
removes blue first so the disc reddens through the transmittance LUT, and once the sun passes below the horizon the sky simply has
no light source and goes dark — **because it is dark, not because a curve faded it**. The bright band at the horizon before sunrise
is the sky-view LUT's non-linear horizon parameterisation resolving the lit air below the observer's horizon. All three of your
stated requirements are consequences, not features to be authored.


PART THREE — SUN AND SKY AS LIGHT, NOT AS BACKDROP
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
The part that is genuinely expensive, and the part a raster engine does not have to solve.

Slate is a ReSTIR path tracer. A skybox that is not sampleable lights nothing: you would get a beautiful sky over a black scene.
So sun and sky must enter the reservoir as emitters.

  **Sun** — a directional emitter with a real solid angle (≈ 0.53° as seen from Earth; per-world, since it depends on distance and
  stellar radius). Sampling the disc rather than a single direction is what gives correct penumbrae. This is a small, well-defined
  addition to the existing light sampling.

  **Sky dome** — an environment emitter. Importance sampling it needs an alias table over the sky-view LUT, rebuilt when the sun
  moves appreciably. **The alias machinery from R6 already exists and is proven** (`PickLight`, `Luminaires[]`, the threshold/slot
  pair) — this is the single largest piece of luck in the plan. The work is building the table from a LUT rather than from triangle
  emitters, not inventing the sampler.

  **Moon** — a weak emitter at night, with a phase term from sun-moon-observer geometry. Textured, per your decision. Per-world
  texture slot so each world can have its own moon, or several.

Budget note for the GTX 1650 SUPER: the LUT passes are trivial (hundreds of µs). The real cost is every ray miss sampling the
environment, and the reservoir carrying sky samples. **This is the thing to benchmark before committing to S4**, and it is the
one place in the plan where I would expect an unpleasant surprise.


PART FOUR — COORDINATES
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
100 000 km at 32-bit float gives ~8 m precision. Unusable. The research is unanimous, and Star Citizen, Flax and UE5 LWC all
converge on the same answer:

  · All world and entity positions in **64-bit** (`Real64`, already the codebase's convention).
  · Simulation, physics origin and culling in 64-bit.
  · **Render camera-relative**: subtract the camera's 64-bit position on the CPU, upload 32-bit offsets. Precision then scales with
    distance *from the camera*, which is exactly where it is needed. The GPU never sees a large coordinate.
  · Keep the existing reversed-Z depth.

**Camera-relative rendering is not world-origin rebasing, and the distinction is the whole point for you.** Rebasing mutates
authoritative positions and is notoriously fragile in networked games — the Unreal community explicitly warns against the built-in
version for multiplayer. Camera-relative changes nothing authoritative; it is a per-frame view transform. Simulation stays in one
absolute 64-bit frame, so **there is exactly one coherent world for networking and streaming to reason about**. That is what
satisfies your sync requirement, and it is why this must be done before there are two worlds rather than after.


PART FIVE — PORTALS
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
Two halves, and they are independent: how a portal *looks*, and how an entity *crosses* it.

Rendering — ray teleport
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
A ray striking the portal plane is transformed by the portal's relative transform and continues tracing in the destination world's
BVH, under the destination world's atmosphere. No second render, no stencil, no render target, no recursion limit beyond ray depth.

Reflections, shadows and GI cross the portal **correctly and for free**, because they are the same rays. A raster engine has to
solve each of those separately and mostly cannot: this is a genuine architectural advantage and worth designing around deliberately.

Cost: the traversal loop must switch which TLAS and which atmosphere it is querying mid-ray. That is a real change — the ray needs
to carry a world ordinal, and the traversal needs an indirection through a per-world TLAS table. Tractable, but it touches the
hottest loop in the renderer, so it wants its own phase and its own benchmark.

Crossing — entity duplication with half-clipping
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Understood, and it is the right model. An entity straddling the portal exists as **two instances**, one in each world, at
transform-mapped positions, each **clipped to its own side of the portal plane**. The near half renders in world A, the far half in
world B, and they meet exactly at the plane so the entity reads as one continuous object crossing a threshold.

  · Clipping is a plane test in the shading path, not geometry surgery — the alpha-mask discard path from R4b-3 is the precedent.
  · Physics: one authoritative body, the duplicate is a ghost that mirrors it. Two authoritative bodies would fight.
  · Handover when the entity's origin crosses the plane: authority moves to world B, the ghost becomes real, the old body becomes
    the ghost. Momentum and orientation are transform-mapped, not re-derived.
  · **Multiplayer**: the duplicate is derived state, never replicated. Each client computes it from the authoritative body and the
    portal transform — both of which it already has. Same principle as the clock: replicate causes, derive effects.

Recursion (portal visible through a portal) is bounded by ray depth and needs no extra machinery, which is the second free win
from the ray-teleport approach.


PART SIX — SKY FURNITURE
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  **Stars** — catalogue or procedural point set, per world, rotated by the clock. Must be attenuated by the atmosphere's
  transmittance or they will show through daylight. Cheap.

  **Sun body** — per your note: a sphere with yellowish noise, a real object at a real orbital distance, normally seen only as the
  disc through the atmosphere, and visible as geometry in the debug overview or from space.

  **Moon** — textured, per-world slot, phase from geometry, weak night emitter.

  **Asteroid belts** — instanced bodies on shared orbital elements with per-instance phase offset. The D1–D6 instancing path
  already handles moving instanced geometry. Worth flagging: a dense belt is very likely what finally **trips the R8 GPU-BVH gate**
  (~8 cars / ~15 000 moving triangles). That is fine — it means the belt work and the R8 work should be planned together rather
  than discovering the collision late.

  **`WorldOverviewSequence`** — the debug fit-to-view trigger. Suspends normal rendering, frames all world origins, draws each world
  as a low-resolution sphere plus its atmosphere shell (the transmittance LUT alone gives a convincing limb). No geometry residency
  required, which is exactly why it is cheap and safe to leave in. `Sequence` is an authorised role suffix; `Director` is not.


PART SEVEN — RESIDENCY
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
A world is resident if: the local player is in it, OR a networked player is in it, OR it is visible through an open portal, OR the
overview is open (at low detail).

A non-resident world keeps only its `AtmosphereConfiguration`, `WorldOrbitRecord` and constant LUTs — kilobytes. It therefore
remains **fully simulated** (suns rise, moons orbit, belts advance) with no geometry resident at all, because all of that is
closed-form from the clock. Re-entering a world is a geometry load, never a state resync. This is the payoff for making the clock
authoritative in Part One.


THE LADDER
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
        S0  ✅  spatial tap jitter                    DONE — 2ba89b5, awaiting hardware confirmation
        W1      64-bit positions + camera-relative    foundation; before multi-world content exists
        A1      clock + Keplerian orbits              CPU only, offline proof against known ephemerides
        A2      transmittance + multi-scattering LUTs offline proof against published reference values
        A3      sky-view LUT + ray-miss sampling      FIRST LIGHT — visible sky, correct sunset and sunrise
        A4      sun as ReSTIR emitter                 physical sun, soft shadows, correct penumbrae
        A5      sky dome as environment emitter       scene lit BY the sky; benchmark here
        A6      aerial perspective froxels            distance haze
        A7      stars, moon, sun body                 night sky
        M1      per-world atmosphere records          multi-world, one camera
        M2      portal ray teleport                   see into the other world
        M3      entity duplication + half-clipping    walk through it
        M4      residency + WorldOverviewSequence     scale-out
        M5      asteroid belts (with R8 GPU BVH)      plan jointly; the belt likely trips the gate

Sequencing rationale: **W1 before everything multi-world** — retrofitting 64-bit coordinates after multi-world content exists is
substantially more painful than doing it while there is one world. **A1 before A2** because the LUTs need a sun direction to be
built against. **A3 is the first visible milestone** and is worth reaching quickly: it gives you the sunset you asked for, before
any of the expensive emitter work.

OPEN QUESTION — only one left
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Scale. Is 100 000 km literal? It determines whether worlds sit in one solar system with real orbital relationships (so world B's
sun is potentially world A's distant star, and the overview is a genuine system map), or whether they are independent pockets that
merely happen to be far apart. Both are supportable and the plan does not change much either way — but it decides whether the
orbital elements are shared against one barycentre or private per world, and that is easier to settle now than later.
