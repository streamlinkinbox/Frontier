# Why the image stopped getting crisper, and why the patches stopped moving

Written 2026-09-26, in answer to three reports from the owner's machine (Windows, GTX 1650 SUPER,
NVIDIA 576.40):

1. “before it used to render after it converged — it would still render, giving more crisp results; now it doesn’t”
2. “the clusters we added don’t seem to change when I move closer / further”
3. “geometry patches aren’t updating”

This file is the diagnosis and the measurements. `Docs/ProgressiveRefinement.md` and `Docs/PatchGeometry.md`
describe the systems themselves; both are updated to match.

⚠️ **Nothing here has been executed on a GPU.** Everything below is CPU evidence from the shipped shader text
(the mechanical C++ port), the shared C++/GLSL policy headers and the real registration path, plus a full
SPIR-V lowering of all 22 shaders. The executable **and the shaders** must be rebuilt together.

---

## ① “It stops getting crisper”

Two independent causes, both real, both fixed.

### 1a. The à-trous fade was one weight for five very different levels

The filter is five à-trous levels with tap spacings 1, 2, 4, 8, 16 px. Since the previous change each level
faded with the pixel's own valid sample count, `33 / count`, and the same weight was used for **every** level.
That converges to an identity in the limit, but slowly and uniformly — and the levels do not cost the same
amount of detail. Level 0 mixes the neighbours one pixel away; level 4 mixes neighbours **thirty-two** pixels
away. At 256 samples the old policy still blended 13 % of a 32-pixel-wide blur into the presented image, and
that is what a held frame looks like when a reviewer calls it “blurry”.

The fade is now keyed to the level's own tap spacing as well as the age:

```
Strength(count, step) = (33 / count) ^ (1 + log2(step))      for count > 33
                      = 1                                     otherwise
```

The wide levels retire first — the same ordering SVGF derivatives use when they drop levels as history grows,
expressed as a continuous weight so nothing pops on the frame a level would have been dropped. Below the
bound every level is at full strength, so young and freshly disoccluded pixels are filtered **bit-identically**
to before (the gate asserts this).

Measured with the shipped shader's C++ port on a new fixture carrying detail at five spatial scales
(1/2/4/8/16 px bands), mean absolute detail bias, lower is crisper:

| valid samples | 1 px band | 2 px | 4 px | 8 px | 16 px | all |
|---|---|---|---|---|---|---|
| ≤ 33 (unchanged) | 0.00989 | 0.00988 | 0.00979 | 0.00958 | 0.00922 | 0.00968 |
| 64 — before | 0.00684 | 0.00830 | 0.00844 | 0.00806 | 0.00730 | 0.00780 |
| 64 — after | 0.00539 | 0.00575 | 0.00464 | 0.00331 | 0.00200 | **0.00425** |
| 256 — before | 0.00221 | 0.00323 | 0.00340 | 0.00313 | 0.00259 | 0.00292 |
| 256 — after | 0.00128 | 0.00110 | 0.00063 | 0.00035 | 0.00017 | **0.00071** |
| 8192 — after | 0.00004 | 0.00003 | 0.00002 | 0.00001 | 0.000004 | 0.00002 |

At 256 samples the frame keeps **4.1×** more of its detail overall and **15.6×** more of the 16 px detail.
The detail amplitude in the fixture is 0.02, so “0.00292” is 15 % of the feature gone and “0.00071” is 3.5 %.

### 1b. A twinkling star restarted the whole accumulation, every frame

The frame loop restarts the accumulation whenever the packed post record changes, comparing the bytes before
`Weather` (weather is deliberately excluded — it composites after clean lighting history, so wind must not
reset GI). `PostStarEffects.w` sits inside that compared head and is **seconds**: `CelestialSequence::Tick`
advances it every tick whenever stars are visible, and `StarTwinkle` defaults on.

So in any scene with visible stars the record differed on every single frame:

* accumulation index pinned at 1 → `count` never grew,
* `ProgressiveDenoiseStrength` therefore pinned at 1.0 → the filter never faded,
* the image could not converge no matter how still the camera was held.

The twinkle is now excluded from the compare exactly as weather is. It is a per-frame modulation applied to
the **sample** (`PostRecords.slang`: `Flux` from `sin(T)`, `T` from `PostStarEffects.w`), like the lens flare —
so leaving history alone does not corrupt the mean, it converges it. A held camera settles on the mean twinkle
instead of trading the entire frame's convergence for it. Every other field in the head still restarts.

### 1c. Every restart now names itself

A progressive integrator that never converges looks exactly like a broken one, and the difference is a fact
the frame loop already knew and threw away: **which** comparison restarted the history.
`ReSTIRIntegrator::ResetAccumulation` now takes a string literal, and both readouts show it:

* the scene telemetry line — `… | frame 412 (restart: camera move) | …`
* the render panel — `Frame 412 accumulated` / `Restart camera move (57 total)`

A still camera on a settled scene must leave the reason and the total **alone** while the frame count climbs.
If they climb together, the name on screen is the subsystem to look at — no guessing, no instrumented build.
Named sources: camera move / camera turn / viewport resize / camera projection / sky record / moon record /
post record / material apply / instance motion / debug popup / exposure / and one per quality dial.

### What was ruled out (measured, not assumed)

* **A 256-frame cap.** There is none. `BakeFrameCount` only raises the “Initial accumulation ready — refinement
  continues” notification; the integrator keeps incrementing and `ResolveSurface` keeps updating the mean.
* **The SVGF moving-history clamp pinning a still frame at 32 samples.** `kMovingHistoryBound` applies only when
  `prevPx != pixel`. Motion vectors are built from **unjittered** current and previous clip positions
  (`VisibilityRaster.vert/frag`), so a still camera yields exactly zero motion and `prevPx == pixel` — the count
  stays unbounded. AA jitter is applied to `gl_Position` only, after the motion attributes are written.
* **History ping-pong / denoise binding 4 format.** Checked by `Tools/Tests/TestDenoiseSafety.py`; unchanged.
* **Cloud-shadow drift as a per-frame reset source.** `FoldShadowDrift` uses `ShadowTimeSeconds`, a frozen slider
  value, not a clock.
* **An animating sun.** `CelestialClock::Animate` defaults **false**; with the defaults a six-tick CPU run shows
  the sky, moon and post records byte-identical every tick.

---

## ② / ③ The patches were not broken — their error number was ~10× too big

The preview selects a patch's coarse alternative when its projected geometric error is within tolerance:

```
projected = focal · (error · scale) · (1 + (lateral + radius)/nearest) / nearest        ≤ tolerance (px)
```

`error` is the patch's object-space deviation, produced by the bake. v1 computed it as

```
radius[a] = max(radius[a], radius[b] + |ab|)         // the full edge length, summed along the collapse chain
```

which is a chain **sum of edge lengths**, not a deviation. On the native 960-triangle sphere it reported
0.19–0.47 object-space units for patches whose surface actually moves by a few hundredths. The selector divides
the camera distance by exactly that number, so a 10× over-statement is a 10× further switch: nothing changed
until the camera was hundreds of metres from a 2 m ball. That is report ② and ③ — not “not wired”, but
“wired to a number that never fires at a distance a person would dolly”.

The bake now **measures** the deviation: the one-sided distance from the original patch surface to the
simplified one, sampled at every fine vertex, every fine edge midpoint and every fine triangle centroid
(6 samples per fine triangle, ≤ 768 per patch), each closed against every simplified triangle with the exact
point-in-Voronoi-region closest-point test. It is a **sampled** Hausdorff estimate, not a certified bound —
the true maximum can sit between samples — and for a ≤128-triangle patch the sampling is dense against the
triangles that remain.

Measured, native sphere (radius ~1, 8 patches, 960 triangles):

| | patch errors | first distance where detail drops |
|---|---|---|
| v1 chain sum | 0.194 – 0.468 | ≈ 300 m |
| measured (v2) | 0.018 – 0.041 | **21.6 m** |

Torus: 0.014 – 0.037, switch at **17.8 m**.

The patch cache moves to **v2** (`.frontier/cache/patch-geometry-v2/`, version word 2) so no v1 entry can keep
the old distance alive. `FRONTIER_PATCH_CACHE` still overrides the location.

### The tolerance is now a dial — F6

One alternative at a strict one-pixel bound is, correctly, a rare event: one pixel of geometric error is a
tight budget. To watch the transition at a normal viewing distance — and to confirm from the screen that the
selector is live — **F6 in the debug popup (F3) cycles the preview's screen-error tolerance 1 → 2 → 4 → 8 px**.
It is shown in the popup (`patch error 4 px`), persisted as `[render] patch_error_pixels`, and written once per
frame into `Projection.z`, so both cull phases and the vertex shader cannot disagree about it.

First distance at which the selection drops, 720 p, 60° vertical field of view:

| tolerance | sphere | torus |
|---|---|---|
| 1 px (default) | 21.6 m | 17.8 m |
| 2 px | 12.8 m | 9.7 m |
| 4 px | 7.3 m | 5.6 m |
| 8 px | 4.5 m | 3.6 m |

Triangle counts for the sphere (960 fine → 550 fully coarse), by camera distance:

| | 1.7 m | 3 m | 5 m | 8 m | 12 m | 20 m | 35 m | 60 m |
|---|---|---|---|---|---|---|---|---|
| 1 px | 960 | 960 | 960 | 960 | 960 | 960 | 800 | 550 |
| 2 px | 960 | 960 | 960 | 960 | 914 | 800 | 550 | 550 |
| 4 px | 960 | 960 | 960 | 914 | 574 | 550 | 550 | 550 |
| 8 px | 960 | 960 | 914 | 550 | 550 | 550 | 550 | 550 |

### What did NOT change, deliberately

* Selection is still **preview-only**: `Control.z ∈ {14, 15}`, i.e. the quick tile must read **Patch Tiles** or
  **Tiles + Wireframe**. Normal shaded rendering, ray traversal, shadows and luminaire sampling always use the
  fine triangles. Primary/secondary surface correspondence is not implemented, and a coarse raster against a
  fine ray scene is a self-shadowing mismatch, not an optimisation.
* Still **one** alternative per patch (≈ half the triangles). This is not an adaptive hierarchy: past the switch
  distance nothing further happens. Patch tile **colours are stable by design** — in `Patch Tiles` the tiles will
  look identical near and far, and the triangle change is only visible in **Tiles + Wireframe**, or in the
  triangle counter in the F3 popup, which drops as the alternative is taken.
* Protected materials (glass, transmission, subsurface, emissive, layered, uncertain textures, thin slabs) never
  take an alternative, at any tolerance or distance. The gate re-checks this at 1000 distances.

---

## Which “clusters”? — the HTML lab is a different thing, and it is fine

`Experimental/FrontierEditor/cluster-lod.html` (the Nanite-style cluster-LOD lab, `Docs/ClusterLodLab.md`) is an
HTML/Canvas demo with no engine integration. It was checked here and it does respond to distance — same scene,
`buildMesh` triangle count by eye distance: 18 240 (2.2) · 11 248 (4) · 4 928 (8) · 2 784 (16) · 1 552 (24).
If “the clusters don't change” was about the lab, that is not reproducible; the report matches the **engine**
patch preview, which is what ② and ③ above fix. The two systems share a name and nothing else.

---

## Validation run here

| gate | result |
|---|---|
| `bash Tools/Build/CheckPatchGeometry.sh` | PASS, **83 322** CPU checks — new: switch-distance bounds (> 3 m at 1 px, < 150 m at 1 px, < 15 m at 8 px), monotonicity in distance at every tolerance, measured deviation < ¼ of the patch radius |
| `bash Tools/Build/CheckProgressiveDenoise.sh` | PASS — five-scale detail table above, per-band improvement, young-history identity, disabled-filter identity, integrator past 8192 |
| `python3 Tools/Tests/TestDenoiseSafety.py` | PASS — 102 429 arithmetic checks + descriptor/barrier/source guards |
| `bash Tools/Build/CheckShaders.sh` | **GREEN — 22/22 shaders lowered to SPIR-V** (glslang built from source here via `Tools/Build/BuildGlslang.sh`; previous runs of this gate reported SKIPPED) |
| `bash Tools/Build/CheckShaderTableParity.sh`, `CheckBuildSourceList.sh` | GREEN |
| `bash Tools/Build/CheckDriverProgress.sh`, `CheckPipelineCache.sh`, `CheckPerformanceTelemetry.sh` | PASS / GREEN |
| `bash Tools/Build/CheckTelemetryProbe.sh` | **RED before and after this change** — `SyntheticGpu` in the gate has no `HistorySnapshotMilliseconds`; verified identical on the untouched tree, so it is pre-existing and not part of this work |
| C++20 syntax check with real dependency headers | `GameExecution.cpp`, `VisibilityExchange.cpp`, `ReSTIRIntegrator.cpp`, `DiagnosticInspector.cpp`, `ConfigurationRegistry.cpp`, `SceneStructure.cpp` |

The cold-load cost of measuring the deviation is pruned rather than paid: a fine triangle that survives into
the alternative is skipped (it is part of the simplified surface), and a candidate triangle whose bounding
sphere is already further than the incumbent is skipped. Identical errors, 4.3× faster — the sphere's eight
patches measure in 2.04 ms, 15 % of their cold bake. It is cached (`.pgeom`) after the first load either way.

The whole change is ONE commit on top of the imported engine tree, so it lands on
`streamlinkinbox/Frontier@arena/01a0c77d-frontier` as a single conflict-free cherry-pick — the import commit's
tree is byte-identical to `ceee3d2`'s, and the cherry-picked result's tree hash matches this branch's exactly.

## How to confirm it on the machine that has the GPU

1. Rebuild **executable and shaders together** (the patch cache rebakes itself; v1 entries are ignored).
2. Shaded view, native resolution, hold the camera still past the “Initial accumulation ready” notification.
   The scene line must show `frame N (restart: …)` with **N climbing** and the restart total steady. If N sticks
   at 1, the reason printed next to it is the subsystem that is still wiggling — that is the whole point of it
   being on screen.
3. Compare a fine texture or a specular edge at ~30 s of hold against the same frame at one second. The first
   second is unchanged by design; the difference is everything after it.
4. Patches: F3 to open the popup, quick tile to **Tiles + Wireframe**, F6 until it reads `patch error 8 px`,
   then dolly an opaque smooth mesh between 3 m and 10 m. The wireframe inside the tiles must thin out, the tile
   colours must not change, and the popup's triangle count must drop. Glass and emissive objects must keep their
   full wireframe at every distance.

---

# Second pass — 2026-09-26 evening, from the owner's screenshots

Five items came back from the running build. What each one was, and what changed.

## ① "The clusters are there but don't seem to update"

The screenshots are `Patch Tiles` at ~10 m over the showcase grid. **Nothing in that image can tell you whether a
patch swapped**, because the preview was built to keep a patch's colour *stable* across a detail change — that was
a deliberate choice (identity must not flicker) and it made the feature invisible without the wireframe.

Two additions, so the answer is on screen either way:

* **The alternative now shades itself.** In `Patch Tiles` and `Tiles + Wireframe` a patch drawn through its coarse
  alternative keeps its hue and drops to 34 % value. Dolly out and the tiles darken one at a time as each patch
  crosses its error bound; dolly in and they light back up. Identity still never changes hue.
* **The F3 popup counts them**: `patch coarse 312/1240 → 52 100 fine, -25.3%` — how many drawn clusters took the
  alternative, what the frame would have cost at full detail, and the saving. Two new GPU counters
  (`kCounterCoarseDrawn`, `kCounterTriangleFine`) written by the cull, read back with the existing funnel.

With the measured error from the first pass, a 1 m sphere switches at ~10 m at the strict 1 px bound, so in that
exact screenshot most of the grid should already be dark; **F6** raises the tolerance (1 → 2 → 4 → 8 px) and pulls
the transition into arm's reach.

## ② "Are those fireflies? I wanted to see the flakes"

They are **sampling noise, not flakes** — and the sphere in the inspector (`grid_r10_c19`) has no flakes to see.
The showcase grid is one material study per row, and glints live on **row 12** (`ShowcaseStructure.cpp`, case 12:
Deliot–Belcour flake density 1 → 8 across the columns over 0.8-metalness hue-tinted metal). Row 10 is the
dielectric → metal morph: metalness sweep, roughness 0.22, no `slate_glint_*` at all. Fly to `grid_r12_c00` …
`grid_r12_c19` and the flakes are the thing that changes along the row.

The speckle itself is the path tracer at a low sample count — roughness 0.22 at metalness 1.0 is the classic
firefly material, and every one of those frames was taken with the camera moving, which restarts the history by
design. Hold still: with the first pass's fixes the frame count climbs, the speckle averages out and the image
sharpens instead of stopping at "blurred". If it does NOT settle, the scene line now prints the reason
(`frame 1 (restart: …)`) and that name is the bug.

Rain is not the answer here: precipitation at 12 mm/h is simulation state feeding the media, and nothing in
`Engine/Shaders` draws drops.

## ③ The disc shelf over the viewport is gone

Twelve editor proxies for sky, sun, stars, moons, flare, wind, the cloud deck, precipitation and the two global
fogs were laid out as a camera-facing shelf across the top of the render — markers for entities that have no
position. They are removed. The **local cloud and local fog keep their markers**, because those are real
world-space centres, they are how the volume is grabbed, and the gizmo now follows them (⑤). Global systems stay
where they belong, in the outliner.

## ④ The outliner moves now

* **Folding is a movement.** Each row carries an open phase that chases its collapsed state with a ~90 ms time
  constant; a folder's children draw at that phase — full height at 1, nothing at 0, every height between — with
  their ink fading as the square of it and a clip rect so nothing spills. Nested folders multiply, so a
  grandchild folds with its grandparent. A subtree whose phase reaches 0 is skipped exactly as before, so a
  closed tree still costs nothing.
* **The chevron sweeps** its quarter turn with that same phase instead of snapping (the two end poses are the
  float-identical ones it drew before).
* **The wheel glides.** The tree takes the wheel itself (`NoScrollWithMouse`) and eases toward the target
  (frame-rate independent, stops dead under a pixel so a resting list is bit-stable). A pick that scrolls itself
  into view still wins its frame; the glide adopts the new position on the next one.

## ⑤ Local volumetric fog and clouds move with the gizmo

Selecting the local cloud or the local fog — in the outliner or by its viewport marker — now seats the transform
gizmo on the volume's centre and drags it. Translate only, on purpose: a centre has no orientation and no scale,
and the extent is the inspector's own figure. Escape restores the centre from the press, exactly as it restores
an instance's world, and the release commits. The move restarts the accumulation (`restart: volume move`),
because the lighting in the history was gathered through the volume where it used to be.

One shared answer decides all of it — `EditorInspectorSequence::VolumeCentre` — so the marker, the gizmo and the
drag cannot disagree about which entities are movable.

## Also worth knowing, from the log you sent

* `SceneDecode` took **95.8 s**. That is the patch bake running COLD: the v1 → v2 cache bump invalidated every
  entry, so every one of the 3 590 clusters rebaked on that launch. It is cached afterwards
  (`.frontier/cache/patch-geometry-v2/`) — the second launch should be back to a few seconds. The measurement
  itself is ~15 % of that; the greedy collapse loop is the rest.
* The freeze after `CpuAnimationMirrorCapacity` is not something this change can explain from here — the line
  after it is the first frame's work. If it happens again, the startup CSV
  (`Build/Diagnostics/startup-*.csv`) plus the last `[GPU startup]` line is what identifies the stage.

## Gates for this pass

`CheckShaders.sh` **22/22 to SPIR-V** (ClusterCull, SurfaceResolve and the rest, with a glslang built here),
`CheckPatchGeometry.sh` 83 322 checks, `CheckProgressiveDenoise.sh`, `TestDenoiseSafety.py`, and C++20 syntax
checks of every touched translation unit against real headers. `CheckEditorVisualProofs.sh` is RED before and
after — its source list has drifted (22 undefined references) and the icon path needs thorvg, which is not
installed here; `CheckTelemetryProbe.sh` likewise. Neither was disturbed by this work, and **none of this ran on
a GPU**.

---

# Third pass — the pointer while flying, 2026-09-26

**Report:** flying the scene (right mouse held + WASD) ends up hovering rows in the outliner and opening the
Construct menu, which is then not visible.

Two separate causes, both fixed.

* **Shift+A is the Construct menu AND “boost + strafe left”.** `EditorHost::RecordTabAdd` opened Construct on
  `Shift + A` with no regard for what the camera was doing, so every boosted leftward strafe opened it — behind
  the viewport, where you would not see it. The shortcut is now refused while the camera steers.
* **GLFW puts the cursor in disabled mode while the right button is held**, which means the pointer no longer
  sits where you left it: it travels with the look. ImGui kept receiving those positions, so rows lit up under a
  cursor that is not drawn, and a click landed wherever the look had carried it. `RenderScheduler::Present` now
  raises `ImGuiConfigFlags_NoMouse` for any frame in which the camera is steering: ImGui discards the position
  and the buttons for that frame — hover, clicks, drags and `WantCaptureMouse` all go quiet — and lowers it again
  on release. Flight itself reads nothing through ImGui, so the camera is untouched.

One flag decides both (`EditorHost::AssignCameraSteering`, set from `FlyThroughSolver::IsSteeringActive()`), so the
pointer and the shortcuts can never disagree about who owns the input.

If the Construct menu is still invisible when you open it **deliberately** (the dock's + or Shift+A while not
flying), that is a separate defect in where the panel places itself — say so and it gets its own pass.

## Viewport header

`Docs/Design/ViewportHeader.html` is the redesign proposal: today's rail annotated with what is wrong with it,
then three options drawn with the engine's own tokens, live (the modes and toggles click), plus every state of the
recommended option, the narrow-rail behaviour, and a table of what each piece costs in `ViewportPanel.cpp`.
Nothing is implemented yet — it is a mockup waiting on a choice.

---

# Fourth pass — the viewport rail, option A

Built in `Engine/Editor/ViewportPanel.cpp::RecordBar`, to the mockup:

* **Three zones.** Left the scene (brand · the two dock toggles as one segmented pair · Add), centre the run,
  right the view (projection · markers · status · settings). The centre is centred in the row and clamped so it
  can never collide with either side.
* **The mode pill is the state.** `Edit | Simulate | Play` over the existing `Transport_` / `Paused_` — no new
  state was invented. The running segment carries the tint (indigo simulate, green play) and reads **Paused** in
  amber when held, which is what the separate Edit/Play/Paused chip used to say.
* **Pause / step / stop arrive when they mean something.** They ease in over 180 ms whenever the world runs and
  ease out when it stops; a half-arrived button refuses clicks. Their rules are untouched — step waits on pause,
  stop waits on a run.
* **One status, not two.** `Live / Static / Held / Running` + the accumulated sample count, tabular, in the mono
  face. Clicking it toggles realtime (what the old Realtime pill did); hovering names the last restart, so the
  question “why is this not converging?” is answered on the rail instead of in the F3 popup.
* **A 2 px convergence hairline** under the rail: teal as the first 256 samples fill, blue once past them and
  only refining. It is also the rail's bottom rule, so it costs no height.
* **It narrows properly.** Labels retire in one order — markers, then add, then the projection (to `Persp`), then
  the two inactive modes (to their play/simulate glyphs). Below 560 px the rail still holds its height and rests,
  exactly as before.

Fourteen controls became nine at rest. The add menu and the views menu are the same popups, byte for byte,
re-anchored under their new pills.

`ViewportPanel::AssignRenderStatus(samples, target, reason, fovDegrees)` is the only new input; `EditorHost`
passes it through and `RenderScheduler::Present` seats it each frame from the integrator and the camera.

Gates: `ViewportPanel.cpp` and `RenderScheduler.cpp` compile warning-clean under `-Wall -Wextra` with real
headers, `GameExecution.cpp` still compiles, `CheckPatchGeometry.sh` 83 322 checks and
`CheckProgressiveDenoise.sh` still pass. The editor's own visual proof cannot link here (it needs thorvg, and
its source list has drifted) — that is the pre-existing RED noted above, so **the first sight of this rail is on
your machine**.

---

# Open after this session — the backlog

Ordered by who is blocked, not by size. Nothing here is lost work; it is what was deliberately not done, what
could not be done from this sandbox, and what was already open in `Docs/Roadmap.md` before any of this started.

## A · Blocked on your machine (I cannot move these)

| # | Item | What closes it |
|---|---|---|
| A1 | **Every change in these four passes is GPU-unseen.** Patch tiles darkening, the denoiser fade, the new rail, the outliner fold, the volume gizmo, the flight pointer lock. | Rebuild exe + shaders, run, report. Steps are in “How to confirm it on the machine that has the GPU”, above. |
| A2 | **The freeze after `CpuAnimationMirrorCapacity`** on the launch you logged. Not reproducible or explicable from here. | If it recurs: `Build/Diagnostics/startup-*.csv` + the last `[GPU startup]` line. |
| A3 | **Construct menu invisible when opened deliberately** (dock + or Shift+A while not flying). The accidental opening is fixed; whether the panel draws where you can see it is untested. | Open it on purpose and say what you see. |
| A4 | **First-launch cold bake ≈ 96 s** on the showcase (3 590 patches, one thread, synchronous). Cached afterwards. | Decide whether it is worth parallelising the bake or moving it off the load path. |
| A5 | Roadmap **B#11 / B#12** — the standing GPU render verification and the owner's blur + fireflies report. | The same run as A1, with the F3 ReSTIR row open. |

## B · Red before I arrived (pre-existing, untouched)

| # | Gate | Why it is red |
|---|---|---|
| B1 | `Tools/Build/CheckTelemetryProbe.sh` | The gate's `SyntheticGpu` fixture has no `HistorySnapshotMilliseconds`; the probe header does. Verified red on the untouched tree. |
| B2 | `Tools/Build/CheckEditorVisualProofs.sh` | Its source list has drifted (22 undefined references) **and** `IconArt.cpp` needs thorvg, which is not installed in this sandbox. I got it down to the thorvg wall and reverted rather than half-fix it. This is why the new rail has no rendered proof. |

## C · Deferred by design in this session (with the reason)

| # | Item | Reason, and what it would take |
|---|---|---|
| C1 | **Patch LOD stays preview-only** (`Control.z ∈ {14,15}`). Shaded rendering, rays, shadows and luminaires always use fine triangles. | A coarse raster against a fine ray scene self-shadows. Needs primary/secondary surface correspondence first — a real piece of work, not a flag. |
| C2 | **One alternative per patch.** Past the switch distance nothing further happens. | A hierarchy needs more than the 64-byte cluster record's four alternate fields; six shader modules read that stride. |
| C3 | **No hysteresis on the switch.** A camera parked exactly at the threshold can flicker between levels. | A few lines once C2's shape is decided — worth doing together. |
| C4 | **Volume gizmo is translate-only**, and only for the local cloud and the local fog. | A centre has no orientation; the extent is an inspector figure. Scaling the volume from the gizmo is a separate decision. |
| C5 | **The global celestial markers are gone with no way back.** | You asked for them gone. If you ever want them, the honest form is a “Global proxies” item in the Markers control, not the old shelf. |
| C6 | **Header options B and C not built.** | You picked A. They stay in `Docs/Design/ViewportHeader.html` as the alternatives that were weighed. |
| C7 | **The hairline's 256 is duplicated** — `RenderScheduler.cpp` mirrors `GameExecution`'s `BakeFrameCount`. | One shared constant when something else needs it; not worth a header today. |
| C8 | **Precipitation draws nothing.** Rain at 12 mm/h is simulation state feeding the media; no drops are rendered anywhere in `Engine/Shaders`. | A screen-space or volumetric rain pass is a feature, not a fix. Say if you want it scoped. |
| C9 | **Flakes exist only on showcase row 12.** | By design of the material study. Nothing to fix; know where to look. |

## D · Already on the engine's own roadmap (`Docs/Roadmap.md`, unchanged by me)

1. **A#5 indirect coverage 16 % → 100 %** (replay + shift mapping) — the largest measured residual, and the only
   item the roadmap says moves the headline.
2. **A#10 deferred materials** — M4c dispersion, displacement ch20, Tier-B multi-slab.
3. **#26 / #27 environment lighting** — bake the sky probe, then sky as a reservoir candidate (“the smallest
   version that pays”).
4. **C#14–18 dynamic geometry** — built and CPU-gated; each is waiting on one device run.
5. **#28** — the `main` designation, an owner's merge click.

## My recommended next three

1. **Run it** (A1 + A5 together): one session on your card closes more open questions than anything I can do here.
2. **Then whichever of C1/C2 you actually want** — if patch LOD is meant to become a performance feature rather
   than a diagnostic, C1 is the gate and C2 is the shape; if it stays a diagnostic, close them as “won't do”.
3. **B2**, so the editor panels get a rendered proof again — the rail, the outliner fold and the gizmo would all
   be regression-tested by pixels instead of by compilation.

---

# Fifth pass — the clouds

**Report:** “this is what clouds render — even at ultra it renders this blocky clouds — fix that first, then make
it dynamic.”

## What was actually wrong

Measured before touching anything, because three plausible causes needed separating.

| Suspect | Measurement | Verdict |
|---|---|---|
| The hash (`fract(sin(127.1x+311.7y+74.7z)*43758)`) | neighbour correlation over a 64³ lattice: **0.013** at the origin, **0.014** at offset 3000 | **Not the cause.** Replaced anyway (below) for a different, real reason. |
| The fbm's octaves | all three share one lattice orientation — ×2.02 and ×4.10 of the SAME axes | **Cause.** An fbm that never rotates does not hide its lattice, it restates it three times. |
| The interpolation | cubic smoothstep: C¹, so the second derivative jumps at every cell wall | **Cause.** Measured as a step ratio across the wall vs mid-cell of **0.671** (1.0 = invisible lattice). |
| The march | fixed step, first sample at the midpoint, no per-pixel offset | **Cause.** The error against a 4096-step reference is **86.5 % structured** — parallel sample planes, which is the corduroy, and it slides with the camera. |

So: the field was a three-octave value noise on one axis-aligned lattice, joined with a C¹ fade, sampled by a
march whose error is a standing wave. At Feature Scale 1.93 one cell is 1737 m, and from a camera 15 m under a
100 m cloud base you are looking at two or three cells across the whole sky — which is exactly the picture.

## What changed

* **Every octave is rotated** off the previous one (37°/29°, −61°/53°, fixed literals so the CPU mirror
  reproduces them to the bit). Anisotropy of the field measured as axis-aligned vs diagonal gradient:
  **0.978 → 1.010** (1.0 = isotropic).
* **Quintic fade** (6t⁵−15t⁴+10t³, Perlin's own correction) instead of cubic smoothstep. Cell-wall step ratio
  **0.671 → 0.896**.
* **Edge erosion** — a fourth octave subtracted at the coverage edge only, weighted by (1 − density), so the
  wisps get their cauliflower without putting high frequency in the opaque core where the march cannot resolve
  it. (The old comment refusing the fourth octave was right about the body; it is still refused there.)
* **A jittered march.** Every ray's first sample is offset by a hash of the pixel *and* the accumulation index.
  Structured error **0.865 → 0.311** in a single frame, and averaged over 64 frames the error against the
  reference falls **3×** — the banding becomes noise, and noise is what the accumulator already removes.
* **The hash is an integer bit-mix**, not a sine. Not because of the correlation (0.013 is small) but because
  the sine's argument grows with the lattice coordinate — 127.1 × 3000 ≈ 381 000 rad, where float32 has ~0.03
  rad left — so the field's quality depended on where in the world it was sampled. Now it does not:
  correlation **0.0000 / 0.0002 / 0.0047** at offsets 0 / 300 / 3000.

## And now it is dynamic

Drift already existed — the whole field slid with the wind. Sliding is not weather; it is wallpaper going past.
The field now also **evolves**: each octave walks through the noise on its own axis at
`Time × (0.010 + 0.0015 × windspeed)` cells per second — about a cell a minute in a 7 m/s wind. Measured at a
fixed point in space, the density field's correlation with itself is **−0.04 after 60 s** and **−0.17 after
600 s**; it was 1.000 forever on a still day, because on a still day the clock was not even packed
(`if(Moving)` — now `if(Volumes)`).

Cost: nothing new per sample. The evolution is an offset added to coordinates the shader already computes.

## The pictures

CPU renders of the actual shipped field (`Docs/CloudEvidence/CloudFieldRender.cpp`, the owner's own settings:
base 100 m, thickness 1100 m, coverage 60 %, density 2.40, feature scale 1.93, wind 7 m/s):

| | |
|---|---|
| [before](CloudEvidence/cloud-before.png) | flat veil, banding stripes along the horizon |
| [after, one frame](CloudEvidence/cloud-after-one-frame.png) | structure and erosion; the jitter's own noise is visible, as intended |
| [after, 16 frames accumulated](CloudEvidence/cloud-after-accumulated.png) | what the viewport shows once the accumulator has a moment |
| [after, 240 s later](CloudEvidence/cloud-after-240s.png) | a different sky — drifted *and* evolved |

⚠️ These are CPU renders of the density field with a simple single-scatter, not Vulkan captures: they prove the
FIELD, not the frame. The engine's own lighting, phase function and tone map are not in them.

## Gates

`RunShaderMirror.py` **281 checks PASS** — the CPU mirror and the shader text still agree on the density model to
within 0.003 after every change above, which is the check that matters here: the two must not drift.
`CheckShaders.sh` **22/22 to SPIR-V**. `CheckCelestialShadow.sh` and `CheckCelestialContent.sh` **GREEN**.

## Known gap, on purpose

The ground's cloud **shadow** field is staged from a frozen time (`ShadowTimeSeconds`, a slider, not a clock) and
its drift is folded on the CPU from that same frozen value, so it is passed `Evolve = 0`: boiling the shadow
while its drift stands still would be the worse inconsistency. Carrying the staged time into the post record
needs a spare float the 544-byte record does not have. Backlog.
