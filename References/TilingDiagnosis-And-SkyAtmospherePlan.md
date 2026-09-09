══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Tiling Diagnosis · and the Sun / Sky / Atmosphere · Multi-World Plan — research and report, no implementation this session
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

PART ONE — THE TILING. WHAT IT ACTUALLY IS.
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

VERDICT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The tiling is **not** the denoiser and **not** a race. It is the **ReSTIR spatial reuse tap pattern**, and it has been in the renderer
since R6 — long before the denoiser existed. The denoiser only made it visible, by removing the noise that was hiding it.

I was wrong twice. Both bugs I fixed were real, but neither was this.

THE MECHANISM
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
`ReSTIRViewport.slang:893` — spatial reuse taps a **fixed cross at a fixed radius of 11 px**:

        (+11, 0)   (−11, 0)   (0, +11)   (0, −11)

The same four offsets. Every pixel. Every frame. Forever.

That builds a **static graph** on the pixel grid: pixel (x, y) is permanently coupled to (x ± 11, y) and (x, y ± 11) and to nothing
else. The graph partitions the image by (x mod 11, y mod 11). Pixels only ever exchange reservoirs inside their own residue class.

Measured, on a 121 × 121 grid (`/tmp/lat.cpp`):

        fixed cross, radius 11 ....... 121 connected components   ← 121 isolated sample pools
        jittered radius + rotation ...   1 connected component    ← one pool, as it should be

121 disjoint lattices, each pooling a **different subset of light samples**, each converging to its **own slightly different estimate**.
The difference between neighbouring classes is a fixed 11-px periodic pattern.

WHY IT SURVIVES CONVERGENCE — AND WHY IT NEEDS CONVERGENCE
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
This is the part that matters, and it is why every previous theory failed to explain the evidence.

Temporal accumulation does not wash this out. It **cements** it. The same graph is reinforced every single frame, so the per-class bias
is not noise that averages away — it is a fixed systematic offset that becomes *more* precisely resolved the longer you accumulate.

That predicts exactly what the screenshots show, and nothing else did:
  · invisible early (buried under Monte-Carlo noise),
  · sharpest at frame 1136 / 2832 (fully converged),
  · perfectly stable under a static camera,
  · unaffected by every barrier fix.

WHY THE SHAPE KEPT CHANGING
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
        grid  →  two barrier hazards  +  the reuse lattice
        lines →  one barrier hazard   +  the reuse lattice
        grid  →  the reuse lattice alone, now unobstructed

The shape changed each time because I kept removing *other* real defects layered on top of it. The lattice was constant throughout.
I read each change as "progress on one bug" when it was actually "one of three bugs removed". That was the reasoning error.

WHAT I RULED OUT, WITH NUMBERS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Recorded so none of these gets re-litigated later.

  À-trous stepped lattice ......... peak structured residual **0.0006** against a 0.5 signal, and it *falls* with sample count.
                                    Three orders of magnitude below visibility, and the wrong trend for "appears after a while".
  Per-level tolerance loosening ... cuts that residual 15×, but collapses a real 0.200 silhouette to **0.034**. Rejected: trades an
                                    invisible artifact for visible edge damage.
  RNG seed aliasing ............... plausible on inspection (`x + y*7919 + f*1000003` overlaps the pixel and frame axes within ~5.7
                                    frames, and (917, 505) exactly repeats a seed 4 frames later). **Measured and dismissed**:
                                    neighbour sample-path correlation ≤ 0.008, and a separated per-axis hash scores *no better*
                                    (0.00869 vs 0.00887). Not the cause. Do not "fix" the seed for this reason.

THE FIX, WHEN WE IMPLEMENT IT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Standard ReSTIR practice, and what every shipping implementation does: **jitter the spatial taps per pixel and per frame**.

  · rotate the cross by a per-pixel, per-frame random angle,
  · jitter the radius inside a band (≈ 4–16 px) instead of a constant 11,
  · keep the existing normal/depth validation exactly as is.

Cost is a couple of extra random draws per tap. It makes the reuse graph fully connected, which is what the estimator assumes in the
first place — so it is a correctness fix, not a tuning knob. Proof harness will assert component count == 1 over a frame window, plus
the existing unbiasedness checks so the jitter cannot introduce bias.

Recommendation: **fix this before starting the sky.** It is small, it is well understood now, and an atmosphere with a moving sun will
otherwise bake the same lattice into every outdoor frame.


PART TWO — SUN · SKY · ATMOSPHERE
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

RESEARCH SUMMARY
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The field splits cleanly into analytic fits and precomputed-scattering models.

  Preetham (1999) / Hosek–Wilkie (2012) — analytic, O(1) per pixel, no precomputation. Hosek is measurably more accurate than
      Preetham. **But**: ground-level views only, no aerial perspective, few physical parameters, and Hosek *brightens* at low solar
      elevation — the opposite of reality, which is precisely the sunset behaviour you asked for. Disqualifying.

  Bruneton–Neyret (2008) — precomputed multiple scattering, high accuracy, but a large 4-D LUT and a heavy re-bake whenever atmosphere
      parameters change. Ranked "most realistic" alongside Haber in perceptual study, but the LUT cost and its artifacts are real.

  **Hillaire (2020), EGSR — "A Scalable and Production Ready Sky and Atmosphere Rendering Technique."** This is the answer, and it is
      what Unreal itself uses (Hillaire is at Epic). Four small LUTs, no high-dimensional table, dynamic atmosphere parameters with no
      expensive re-bake, ground-to-space views, and it scales down to mobile. Your existing parameter list — planet radius, Rayleigh
      and Mie scale heights, the ozone tent, Mie asymmetry, per-channel scattering — **is already exactly this model's parameter set.**
      You had the right model last time.

The four LUTs:
        ① Transmittance      2D, 256 × 64      constant per atmosphere; rebuild only on parameter change
        ② Multi-scattering   2D, 32 × 32       constant per atmosphere; the geometric-series trick, 64 directions
        ③ Sky-view           2D, 192 × 108     per frame; non-linear in latitude to concentrate resolution at the horizon
        ④ Aerial perspective 3D, 32 × 32 × 32  per frame; froxel volume for distance fog / scattering over the scene

Sunset falls out for free: at low sun elevation the path length through the atmosphere grows, Rayleigh removes blue first, the
transmittance LUT reddens the sun disc, and as the sun passes below the horizon the whole sky loses its light source and goes dark —
physically, not by a fade curve. The bright horizon band at dawn is the sky-view LUT's non-linear horizon parameterisation doing its
job. **This is why the model earns its cost over an analytic fit: it gets your exact requirements for free instead of by hand-tuning.**

FIT TO THIS RENDERER — THE PART THAT NEEDS THOUGHT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Slate is a **ReSTIR path tracer**, not a raster deferred renderer. Hillaire's technique is written for the latter. Two consequences:

  1. The sky is not a skybox — it is a **light source**. Sun and sky must enter the ReSTIR reservoir as sampleable emitters, or the
     scene will be lit by nothing while a pretty sky sits behind it. The sun becomes a directional/solid-angle emitter; the sky dome
     becomes an environment emitter with its own alias table (the alias machinery from R6 already exists and is proven).
  2. Ray misses sample the sky-view LUT instead of returning black.

That makes this **R9-scale work, not a weekend feature**, and it interacts directly with the light-contribution ladder (`High` tier is
still blocked on a descriptor renumber — now well-trodden after R7a).

PROPOSED LADDER
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
        S0   Fix the ReSTIR spatial-tap lattice. Prerequisite; see Part One.
        S1   `AtmosphereConfiguration` record + transmittance and multi-scattering LUTs. Offline proof against published
             reference values; no renderer change yet.
        S2   Sky-view LUT + ray-miss sampling. First light: a visible sky, correct sunset/sunrise, scene still lit as today.
        S3   Sun as a ReSTIR emitter — directional sampling with correct solid angle (≈ 0.53°), soft shadows.
        S4   Sky dome as an environment emitter with an alias table. Now the scene is lit *by* the sky; ambient becomes physical.
        S5   Aerial perspective froxel volume. Distance haze.
        S6   Controls: sun elevation/azimuth (or a time-of-day scalar), turbidity, ground albedo, exposure coupling.
        S7   Night: stars, moon (textured, as you concluded — procedural moons lose to a texture), and the moon as a weak emitter.

Performance on a GTX 1650 SUPER: the LUT passes are trivial (a few hundred µs). The real cost is S4 — every ray miss sampling an
environment map, and the reservoir now carrying sky samples. That is the one to benchmark before committing.


PART THREE — MULTIPLE WORLDS IN ONE LEVEL
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
This is the genuinely novel requirement, and you are right that Unreal does not do it. Unreal has *one* `SkyAtmosphere` component per
level, singleton by design. Your requirement — N worlds, each with its own sun/sky/atmosphere, all simultaneously live in one level,
synchronised for multiplayer, visible through portals — is not expressible in that architecture.

It **is** expressible here, because Slate's atmosphere would be a **record, not a singleton**.

THE CORE IDEA
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Make the world an explicit indexed entity. Every atmosphere-bearing body owns:

        WorldOrdinal          — stable identity, the network key
        WorldOrigin           — 64-bit position in the level's absolute frame
        AtmosphereConfiguration — its own full parameter set
        Sun / moon / star state — its own

The LUT set becomes **per world, not global**: transmittance and multi-scattering are small and constant per atmosphere (cache them,
rebuild on change), sky-view and aerial perspective are per *observer* — so only the world the camera is in, plus any world visible
through an open portal, needs the per-frame pair. Two or three live LUT sets, not N.

COORDINATES — THE HARD CONSTRAINT
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
100 000 km at 32-bit float has a precision of roughly 8 metres. Unusable. The research is unanimous on the fix, and it is what Star
Citizen, Flax and UE5 LWC all converge on:

  · Store all world/entity positions in **64-bit** (`Real64`, which the codebase already uses).
  · Do all simulation, physics origin, and culling in 64-bit.
  · **Render camera-relative**: subtract the camera's 64-bit position on the CPU, upload 32-bit offsets to the GPU. Precision is then
    a function of distance *from the camera*, which is exactly where it is needed. The GPU never sees a large coordinate.
  · Keep the existing reversed-Z depth.

Critically for you: camera-relative rendering is **not** world-origin rebasing. Rebasing mutates authoritative positions and is
notoriously fragile in multiplayer — the Unreal community explicitly warns against the built-in version for networked games. Camera-
relative rendering changes nothing authoritative; it is a per-frame view transform. **Simulation stays in one absolute 64-bit frame, so
multiplayer sync and streaming see a single coherent world.** That directly satisfies your "things need to be in sync" requirement.

PORTALS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Two established approaches: stencil-buffer recursion (Immersive Portals) and render-to-texture (Unity-style). The stencil method
supports portal-in-portal and accurate occlusion queries; the texture method is simpler but breaks recursion.

For a path tracer there is a **third and better option**: a portal is a *ray teleporter*. A ray hitting the portal plane is transformed
by the portal's relative transform and continues tracing in the destination world's BVH and atmosphere. No second render, no stencil,
no recursion limit beyond ray depth, and reflections and GI cross the portal correctly and for free. This is a real advantage of the
architecture over Unreal, and it is worth designing for deliberately. Cost: the traversal must be able to switch which TLAS and which
atmosphere it is querying mid-ray — a real change to the traversal loop, but a tractable one.

WORLD PARTITIONING / STREAMING
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Residency driven by world ordinal first, then by distance inside a world. A world is resident if: the local player is in it, OR a
networked player is in it (multiplayer), OR it is visible through an open portal, OR it is within the debug overview's range at low
detail. Non-resident worlds keep only their `AtmosphereConfiguration` and orbital state — kilobytes — so they can still be *simulated*
(sun angles advance, orbits tick) without any geometry resident. That preserves sync cheaply.

STARS · MOON · ASTEROID BELTS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  Stars    — a catalogue rendered into a cube map per world, or a procedural point set. Cheap either way. Must be attenuated by the
             atmosphere's transmittance or they will show through daylight.
  Moon     — textured, per your decision, with a per-world texture slot so each world can have its own. A phase term from the
             sun-moon-observer geometry, and a weak emitter contribution at night.
  Asteroids— instanced belts around a body; the D1–D6 instancing path already handles moving instanced geometry, and the
             R8 GPU BVH plan (deferred, gated on ~8 cars / ~15 000 moving triangles) is the natural home for the tri counts a belt
             implies. Worth noting: a dense belt likely *triggers* that gate.

DEBUG OVERVIEW
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The "fit all worlds to view" trigger you described is straightforward and genuinely useful: a mode that suspends normal rendering,
places a camera to bound all world origins, and draws each world as a low-resolution sphere plus its atmosphere shell (the transmittance
LUT alone gives a convincing limb). Debug-only, no geometry residency required — which is exactly why it is cheap. Name candidate under
CLAUDE.md rules: `WorldOverviewSequence` (`Sequence` is an authorized role suffix; `Director` is not).

OPEN QUESTIONS FOR YOU
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  1. Scale. Is 100 000 km literal, or shorthand for "far enough to be a separate place"? It decides whether we need true orbital
     mechanics or just static world origins.
  2. Do portals need to be **walk-through** (entity teleport, physics handover) or **look-through only** at first? Look-through alone is
     dramatically less work and proves the multi-world rendering.
  3. Is the sun per world **authored** (an artist sets elevation) or **simulated** (a real orbit with a time-of-day clock)? Multiplayer
     sync is much easier with a simulated clock — one authoritative time value reproduces every world's sun everywhere.

RECOMMENDED ORDER
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
        S0            fix the spatial-tap lattice          small, understood, unblocks clean outdoor images
        S1 – S2       one atmosphere, one world            sunset/sunrise working, the visual milestone you asked for
        W1            64-bit positions + camera-relative   the coordinate foundation; do it before there are two worlds
        S3 – S4       sun and sky as ReSTIR emitters       physical lighting
        W2            per-world atmosphere records         multi-world, still one camera
        W3            portal ray teleport                  see into the other world
        S5 – S7       aerial perspective, night sky        polish
        W4            partitioning + debug overview        scale-out

Doing W1 before W2 is the load-bearing sequencing decision: retrofitting 64-bit coordinates after multi-world content exists is
substantially more painful than doing it while there is one world.
