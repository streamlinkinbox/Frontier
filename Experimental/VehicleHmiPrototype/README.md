# Vehicle HMI Prototype — JavaScript/WebGPU + Jolt WebAssembly

## The combined WebGPU prototype: start here

One app, built from five branches, with each feature taken from the implementation that was better where they competed, and the missing pieces ported from the
user's other repositories. The plan, the evidence behind each choice, the numbers and the known issues are in **[COMBINED_PLAN.md](COMBINED_PLAN.md)**; this page is how to run it.

| Feature | What it is here | Switch (URL) |
| --- | --- | --- |
| **BVH path tracer** | A *software* two-level BVH (TLAS over occupants, binned-SAH BLAS per mesh) traversed in WGSL compute shaders. WebGPU has no hardware ray tracing, so nothing here uses RT cores. | `?renderer=bvh` (default) or `sdf`. `rtx` and `gtx` are accepted as the old names. |
| **ReSTIR GI** | Triangle ReSTIR GI with multi-bounce paths, temporal and spatial reuse, visibility reuse (from the f5 branch). | `bounces`, `reflbounces`, `candidates` |
| **Screen traces** (Lumen's last pillar) | A nearest-depth hierarchy walk over the depth buffer first; on a miss the ray hands off to the scene distance field (SDF mode) or the BVH tracer (BVH mode), resuming where the screen proved the ray unoccluded. | `?screentraces=1` |
| **Clustered lights, system 1: real lights** | A world grid (16 × 16 × 4) of `u32` masks over up to 32 point, spot, flood and street lights, with IES photometry (4 real measured files, 3 authored: `Profiles/README.md`). | `?lightgrid=0`, `?ies=0` |
| **Clustered lights, system 2: emissive surfaces** | Emissive triangles grouped into clusters, binned into the same grid; each cell keeps its 8 strongest, and next-event estimation samples them with exact pdfs under an unbiased partition (BVH renderer). | `?emissive=0` |
| **XPBD tyres** | A 3-D XPBD carcass per tyre (6 compute kernels) whose load/deflection law drives the vehicle's tyre stiffness and contact patch. Ported from Slate `ca517016`. | `?tyres=analytic`, `?tyresubsteps=8…32` (default 32) |

## The Lobby (car-configurator studio, no UI)

Open **`Lobby.html`** — the same renderer in a seamless automotive photography studio: circular cyclorama cove,
suspended luminous softbox (an emissive surface the path tracer samples, plus a matching clustered flood and four
cove washes), dark truss ceiling, glossy turntable floor, and five cars **arranged in a circle**: the graphite hero
on the centre turntable and four display cars on their own turntables around it at radius 10.5 m, nose-in. Each
display car has its **own paint** (sage, brass, steel blue, rose) and its **own wheel-light colour** (teal, amber,
blue, magenta); the hero's rims glow cyan. All five are lit by the one enlarged softbox (24 × 11 m) in a studio
scaled for the fleet (cyclorama radius 20 m). There is **no UI** on the page — no panels, toolbar, HUD or buttons.

On entry the camera **fades in from black** and dollies wide-and-low into the hero framing (any touch takes over).
The orbit camera **lags** behind your drag (eye τ ≈ 0.34 s, look target τ ≈ 0.20 s) and is **contained in the
studio**: it can never rise through the softbox, clip its housing or leave the cyclorama. Left alone it drifts
slowly around the car; after ten idle seconds a **cinematic tour** takes over — wide, front tyre, a side track,
the rear, the rear tyre, the fleet — cutting between shots behind a fade. Any touch, scroll or key stands it down.
There are three camera homes: the orbit, the **driver view** (first person at the wheel — drag to look around,
tap the tablet to bring it close) and the tablet itself. **Tap the car** steps inside; `C` cycles
orbit → driver → tablet, `M` jumps to the tablet, tap off the tablet / `Esc` / `Q` steps back out. The parked
pose is locked in the studio, so the interior cameras sit perfectly still — no idle shake. On the tablet,
**Navigation** is a minimal scenic screen in the style of the reference frame: a white parked-car rail, one teal
route line, and two destination cards — **Proving Grounds** (practice) and **Exploration** (freeroam); *Start
route* fades out and loads that level. Driving with other people is deliberately not a destination: it lives in
the **Drive Room** app, linked at the foot of Navigation.

Three studio rules worth knowing. There is **no derez/rez in the Lobby**: the recovery director never runs
here, teleports and respawns are no-ops, and the vehicle mask is held off — the parked fleet is never
dissolved, hidden or reassembled. The camera is **yours once you take it**: the first touch, drag, scroll or
key press ends attract mode's ten-second fuse, and from then on the cinematic tour and the slow auto-orbit may
only return after a full minute without input — nothing else moves the camera while you are using it. And
every car wears **number plates** front and rear (at the authored `IdPlate` sockets): each plate carries its
driver's nametag and their **Rank** — a coloured badge with one chevron stripe per tier (Bronze, Silver,
Gold, Diamond) plus the Rank in words. The field is one editable list, `LobbyDrivers` in
`Source/LobbyDrivers.js`: hero first, then the four display cars.

```sh
# From Experimental/VehicleHmiPrototype
npm ci && npm --prefix Tools ci                  # Jolt (browser WASM) and Dawn (headless validation)
python3 -m http.server 8080 --bind 0.0.0.0       # then open http://localhost:8080/  (Chrome / Edge with WebGPU)
```

### Tests, all of which have been run

```sh
npm test                                         # unit tests, CPU only (a few seconds)
npm --prefix Tools run validate:modes            # the whole app on Dawn's null backend in 20 URL modes: every shader, layout and pipeline validated
npm --prefix Tools run validate                  # …and validate:screen, :effects, :slate, :orbit, :emissive
RUN_GPU=1 npm test                               # adds the EXECUTED tests: WGSL really runs on CPU cores through SwiftShader (needs registry.npmjs.org once; ~30 min)
npm --prefix Tools run bench:cpu                 # CPU-side costs of the new structures on the real scene
npm --prefix Tools run bench -- --label "screen traces" --baseline "renderer=bvh" --variant "renderer=bvh&screentraces=1"   # one toggle, baseline vs variant, interleaved
```

*Executed* means the shader code runs: the null backend only validates it. The executed tests compare the WGSL with its CPU twin (emissive sampler: 3000 queries; IES lookup:
2400 directions), check the XPBD kernels physically (load rises with deflection and pressure; the four loads sum to the car's weight in the full app), and render whole frames
headless, then compare frames (the light grid is an exact cull; screen traces stay close to the reference; emissive clusters add no energy). GitHub Actions (`.github/workflows/`) runs the unit tests, the 20-mode validation, the executed tests and a toggle-benchmark matrix on every push of
the branch. 🔴 **GitHub runners have no GPU**: the benchmark numbers are shader work on CPU cores (a relative measure), not GPU time, and every report says so.

Documents: [COMBINED_PLAN.md](COMBINED_PLAN.md) (plan, provenance, results, known issues, notes for the C++ port) · [Profiles/README.md](Profiles/README.md) (IES provenance and licences) ·
[Tools/TyreLattice/README.md](Tools/TyreLattice/README.md) (how the XPBD tyre lattice is exported from the C++ engine) · [SLATE_PORT.md](SLATE_PORT.md) (the earlier Jolt/HMI port notes).

---

*The sections below are the inherited descriptions of the parts (f5's vehicle prototype, then the IlluminationHost exhibit); the combined-prototype behaviour above takes precedence.*

Reconstructed from `Experimental/01a141bc-a4a3-72ce-8295-398724231feb.patch`
on `SultanAladin/Frontier-`, supplied branch `arena/9440f4d0-frontier`.
The source patch is not retained. Vehicle `.facet` binaries were regenerated with
`Archive/FacetArchiveCodec.py` from the four authored geometry archives on
`arena/4f07c82a-frontier`; omitted DM Sans fonts come from `@fontsource/dm-sans@5.3.0`
under the included OFL license. The separate reference configurator is preserved
in `../interactive-vehicle-hmi-configurator`.

## Added Garage functionality

The cockpit/dashboard, steering wheel, integrated hexagonal cluster and centre tablet
now follow **Slate `3c987f3`**, not the old binnacle design. HUD scale/position controls
resize only the on-screen diagnostic HUD; the physical interior and seated camera do
not move. See [SLATE_PORT.md](SLATE_PORT.md) for provenance and integration scope.

Open the tablet's **Garage**. Paint, gloss/matte and rim controls update the shared
GPU materials on the **actual driven car**, mirrors and live preview. Click to apply,
or drag a paint swatch onto the preview's body / rim material onto a rim. Drops are
ray-tested against the car's actual facets: background and wrong-material drops are
rejected. Drag elsewhere in the preview to orbit. Cycle **Solid → Ghost → Wireframe
→ Scan**; Scan sweeps an animated plane across the mesh. Inspection modes affect only
the studio preview, not world-car opacity or physics. These are native WebGPU
implementations of the requested interactions, not a Three.js embed or an exact
copy of the configurator's animated ripple transition.

Tyre smoke uses the actual Slate plume transport, source projection and march shaders.
Loose rigid bodies now run **Jolt Physics 1.1.0 in browser JavaScript/WebAssembly**;
custom suspension/tyre/drivetrain physics are deliberately retained and connected via
a collision-impulse chassis bridge. No native C++ build is required.

See [PLAN.md](PLAN.md) for earlier physics/VFX changes.

## Rim, spark and HMI usability corrections

- Shredding removes only rubber. The rim preserves Project Drive's two authored face slots: 1,296 dark faces and
  272 blue emissive accent faces. The dark finish has its own RimBase identity so
  shredding cannot remove it. Both parts remain visible in shadows/mirrors/Garage.
  Garage rim-finish changes affect the accent slot only, not the black base.
- Spark width is bounded to 0.65–1.35 render pixels with a tighter core, preserving
  motion streaks without large close-up particle billboards.
- Press **M** to toggle **HMI** focus / cockpit, or choose **HMI** in the camera selector
  (also available via C). The HMI camera faces and frames the tablet for touch at any
  viewport aspect ratio. Scroll to zoom in HMI, cockpit or passenger view.
- **Cabin · HMI → Tablet / HMI size (not cluster)** scales just the tablet (0.75–2.5×),
  including housing, screen and touch region. Cluster, wheel, dashboard and seated
  eyes are unchanged. The HMI focus camera reframes the enlarged tablet automatically.
  **Reset HMI size / zoom** restores tablet scale, focus distance and cockpit FOV.
  Diagnostic **HUD scale** remains a separate control.

## Display colour and material dragging

Open **Garage** on the tablet to drag paint, gloss/matte finishes or rim finishes
onto the live car preview. A material chip follows your pointer/finger, including
over the GPU-rendered preview. The turntable pauses during dragging. Release on the
matching body/rim surface to apply to the actual car; background/wrong-surface drops
cancel. **Blue accents** restores the original blue metallic/emissive accent slot.

The screen is now display-referred/antiglare rather than HDR sky-reflective glass,
so scene exposure no longer bleaches its authored colours. Both brightness controls
reach zero correctly; set the in-tablet and inspector brightness to 100% / 1 for
normal viewing. Scene emission strength does not boost the UI.

## BVH · Full ReSTIR GI Path Tracer (Default Render Mode)

The default renderer is **BVH · ReSTIR Path Tracer** (`RayTracing: true`, configurable in **Render settings → BVH · ReSTIR Path Tracer** or via `?renderer=bvh|sdf`; `rtx` and `gtx` are accepted as the old names of the same two renderers). It is a *software* path tracer: a two-level BVH traversed in WGSL compute shaders. WebGPU has no hardware ray tracing, so nothing here uses RT cores:

- **Two-Level BVH Acceleration Structure (`Source/RayTracingSpace.js` & `Shader/RayTraversal.wgsl`)**:
  - **Bottom-Level Acceleration Structure (BLAS)**: Binned-SAH BVH (`LeafCapacity = 4`, 12 SAH bins) built over all 56 `FacetSets` (20,854 static/mesh triangles across `ControlVehicle` body/wheels/cabin/roof fixtures, the 3,664-triangle `Proving ground` course mesh, buildings, cones, breakables, stress objects, and street lamp poles/gantries) plus dynamic per-fragment BLAS leaves registered on the fly when breakable props shatter (`RegisterPropFragments`).
  - **Top-Level Acceleration Structure (TLAS)**: Rebuilt every frame (`UpdateTlas`) over all active scene occupants using Binned-SAH, transforming rays into each occupant's local space (`WorldToLocal`) for exact Möller–Trumbore ray-triangle intersection, barycentric smooth vertex normal interpolation, transparent `VehicleGlass` pass-through, and shredded-tyre rubber bypass (`RoleShredded`) so rays strike the underlying metal wheel rim.
- **Full Multi-Bounce Path Tracing (`TraceMultiBouncePath`)**:
  - **Initial Candidate RIS**: Generates $M_{\text{init}}$ (default 2, up to 4) stratified low-discrepancy path candidates per pixel before temporal integration.
  - **Two-Layer PBR BSDF Importance Sampling (`SampleSurfaceBsdf`)**: Importance-samples both the Heitz 2018 GGX Visible Normal Distribution Function (VNDF) specular/clearcoat lobe and the cosine-weighted Lambertian diffuse lobe with exact mixture PDF evaluation.
  - **Next-Event Estimation (NEE) with Inline RIS (`EvaluateDirectNextEvent`)**: At every bounce vertex ($1 \dots B_{\max}$, default 3 bounces), evaluates soft solar cone shadow rays, active tyre-fire point lighting, and **Resampled Importance Sampling (RIS)** over all 32 clustered photometric / IES lights (roadway batwing street lamps, asymmetric low beams, collimated high beams, selective-yellow fog lights, and roof floodlights) with exact TLAS/BLAS shadow rays (`TraceSceneVisibility`).
  - **Multiple Importance Sampling (MIS), Russian Roulette & Infinite-Bounce Tail**: Combines BSDF continuation with emissive hits via balance-heuristic MIS, applies throughput-weighted Russian Roulette termination (`FeatureRussianRoulette`), queries the surface-card radiosity cache beyond $B_{\max}$ for infinite-bounce energy conservation, and suppresses single-path outliers (`FeatureFireflyFilter`).
- **Spatiotemporal ReSTIR GI Reuse & Multi-Bounce Ray-Traced Reflections / Direct Shadows**:
  - **Temporal Reuse (`Shader/ReservoirTemporal.wgsl`)**: Motion-vector reprojection with rigid/dynamic occupant vertex carry (`CarryReservoirSample`), plane-distance geometric rejection, and exact BVH visibility validation.
  - **Spatial Reuse (`Shader/ReservoirSpatial.wgsl`)**: Disk neighbor reservoir merging with the solid-angle reconnection-shift Jacobian $|\partial\omega_r / \partial\omega_q|$, `1/Z` pairwise/tally normalization, and adopted BVH visibility verification.
  - **Multi-Bounce Ray-Traced Reflections & Direct Shadows (`Shader/ReflectionTraversal.wgsl`, `Shader/DisplayRecombination.wgsl`, `Shader/DirectRadiance.wgsl`)**: Traces 1–4 bounce GGX VNDF glossy and mirror reflection paths (`TraceMultiBounceReflection`, default `ReflectionBounces: 2`, configurable via `?reflbounces=1..4` or the **Ray-traced reflections** / **BVH · ReSTIR Path Tracer** inspector cards) across car paint, glass, mirrors, and wet asphalt with direct NEE + BVH shadow rays, surface-card GI, Fresnel/clearcoat continuation, and SVGF spatio-temporal reconstruction.

## Checks

```sh
# From Experimental/VehicleHmiPrototype:
npm ci
npm --prefix Tools ci
npm test
npm --prefix Tools run test:stability
npm --prefix Tools run validate
npm --prefix Tools run validate:effects
npm --prefix Tools run validate:slate
```

Dawn's null backend checks shader/pipeline/command validity, not rendered pixels.
Use a WebGPU-capable browser to assess appearance, interaction and hardware performance.

---

# IlluminationHost — distance-field GI · surface cards · ReSTIR GI (WebGPU)

A self-contained WebGPU exhibit inside the **Frontier Editor** UI: one scene distance field (four camera-centred
toroidal cascades), a surface cache of oriented cards with multi-bounce radiosity, a ReSTIR GI final gather traced
through that field, SVGF reconstruction, glossy reflections — and the Project-Drive ControlVehicle (a port of its
struts, MF6.1 tyres and GT-R powertrain) on the **Frontier proving grounds** and its **training arena** (timed
drills, 136 knockable cones), with GPU drift smoke and tyre fire, burnouts that lay sharp-edged rubber, breakable
props whose debris are convex rigid bodies in the distance field, and a derez / rez effect for every respawn.

**Provenance.** Imported verbatim from `SultanAladin/Slate`, branch `arena/c6cba543-slate`, commit
`e7c4ea47` ("Add WebGPU distance-field GI exhibit: one scene distance field, surface cards, ReSTIR GI,
ControlVehicle"). The later screen-trace commit `194ce3aa` is deliberately not included. Everything after the
import in this folder was developed here. The editor look comes from `Experimental/FrontierEditor` (tokens in
`Docs/FrontierEditorComponents.md`); the fracture algorithm and its material table from `Experimental/FractureEditor`
(Frontier-, `arena/215a6f39-frontier`); the derez presets from `Experimental/ParticleEditor` (same branch); the smoke
design from Frontier's `Engine/VolumetricDynamics`.

## Running

Any static file server works; WebGPU needs a current Chrome or Edge (Safari 26+, Firefox with WebGPU enabled).

```sh
cd Experimental/VehicleHmiPrototype
npm ci  # installs the pinned browser/WASM Jolt package locally
python3 -m http.server 8080 --bind 0.0.0.0
# open http://localhost:8080/
```

## The editor

The page is laid out like the native Frontier editor — **Outliner · Viewport · Inspector** columns with tab
headers and a status bar — in the browser editor's styling (DM Sans, the card/metric/slider/switch vocabulary,
lucide glyphs; `Style/FrontierEditor.css`, `Source/EditorShell.js`, `Source/EditorControls.js`). The outliner
groups the scene; selecting an entry rebuilds the inspector (`Source/InspectorPages.js`):

| Outliner entry | Inspector |
| --- | --- |
| **Render settings** | resolution & quality grade, trace resolution, render scale; ReSTIR temporal/spatial reuse, visibility validation, M limit, indirect intensity; exposure, shadows, reflections, SVGF, TAA; debug views; frame timing and GPU pass timings |
| Global distance field · Surface cache | cascade state and freezing, slice inset; card radiosity and relighting budget |
| Sun & sky | presets (Noon · Golden · Dusk · Night), sun direction, solar / sky / emitter intensity |
| ControlVehicle · Tyre marks | telemetry, **burnout / line lock**, **tyres** (pressures per axle, rigid tyre visuals and **countersteer assist**, wear, failure, puncture / blowout / shred buttons, physical deflection · tread readouts), ignition, thermal grip, **camera rigs**; rubber/rim deposits |
| **Cabin · HMI** | the TabletPanel HMI live in the car (screens, brightness, open an app), **HUD size/position** (no physical cockpit movement), see-through glass and its transmission, mirror views, interior cameras |
| **Smoke & fire** | the Smoke + Fire editor: Domain, Source, Simulation, Appearance, Fire, Presets — every gas quantity as a slider, plus a test emitter |
| **Derez · VFX** | the Particle Editor's Derez cube · Reverse derez · Glitch dissolve presets, cube timing and motion, **the car's own colours or the palette**, face glow, **momentum**, recovery triggers, test buttons |
| **Fracture** · each breakable prop | debris (rigid bodies, distance-field admission, respawn); per prop the Fracture editor: Material, Impact, Fragment quality, Bake, Geometry receipt |
| **Training arena** · Cones | the ten drills with Start buttons, last result and best time; arena reset; the cones |
| Proving grounds · Stress test | zone teleports and the scene budget; the stress pool |

Edits apply live. The eye toggles switch an entity off (smoke, fracture, a single prop, tyre marks).

## Controls

| Key | Action |
| --- | --- |
| W / S | throttle / brake (S reverses once stopped) |
| **W + S** | burnout — line lock holds the front brakes, the rears spin on the limiter; lift S to launch |
| A / D | steer |
| Space | handbrake |
| R | derez and rez at the running drill's start, or the start grid (the rez brings fresh tyres) |
| **C** / Shift+C | camera rigs: chase → cockpit → HMI → passenger → hood → tyre FL → tyre RL |
| T | reset the arena: knocked cones rez back onto their marks, broken props are rebuilt |
| F or F8 | free camera (WASD, Q/E, Shift) |
| drag / wheel | orbit / zoom the chase camera; inside the car drag to look around, click the tablet to use it |
| H · I · P | readout · distance-slice inset · editor columns |
| V | clean view — hides every overlay; V again restores exactly what was open |

## The proving grounds

`Source/LevelStructure.js` declares the level, `Source/CourseStructure.js` its analytic relief (kerbs, the jump ramp),
which the tyres, the fragments and the smoke all read. A 216 × 120 m asphalt pad in a grass surround — the original
120 m square in the west, the training arena in the east:

- **main straight** with red-and-white kerbs, a jump ramp, a start strip, FRONTIER painted on the road and a gravel
  run-off with a tyre wall;
- **skidpad** — a painted 14 m drift ring with cones;
- **burnout box** — a concrete apron with its outline and BURNOUT painted, a tyre wall behind;
- **smash alley** — thirteen breakable props between jersey barriers: wooden crates, an annealed and a tempered
  glass pane in steel frames, octagonal concrete pillars, ABS barriers, a stone wall, a concrete block;
- **hangar** — 28 × 16 × 8 m, open to the south, crimson back wall, a slatted roof that lets sun stripes in and lamp
  strips that light it at night: the bounce-light showcase;
- **container yard**, a **control tower**, a **lit tunnel**, and light poles whose emissive heads light the night
  through the surface cards.

Shapes are authored long-side-on-x and turned into place, tall thin poles, gantry posts and signs are rastered only,
and large structures take coarse volumes and sparse cards, so the occupant distance space stays near 70 MB.

## The training arena

The east wing is Apex Legends' Firing Range for driving (`TrainingDrills` in `Source/LevelStructure.js`,
`Source/TrainingSequence.js`). Every station is live: drive through its start gate (or into its zone) and the drill
starts, the HUD at the top of the viewport runs it, and the result — with a best time kept in the browser — lands
in the HUD and on the Training page. Starting a drill from the page derezzes the car to its start and rezzes the
cones the last run knocked over back onto their marks.

| Station | Drill |
| --- | --- |
| **Launch** | three lanes under green and red gantries: 60 ft and 0–100 splits, the 62 m sprint and trap speed, then stop in the box (22–32 m past the brake line) for the time to count |
| **Slalom** | eight cones 11 m apart, gate to gate, +1 s per displaced cone |
| **Moose test** | the ISO 3888-2 double lane change, coned on both sides: entry speed, clean or failed |
| **Cone bowling** | fifteen pins at the end of a run-up lane, counted 3 s after the foul line |
| **Spin box** | full turns inside an 18 m box without touching a cone or leaving it |
| **Figure-8 drift** · **Skidpad** | angle × speed × time while sliding, chained with a multiplier that grows every 2 s; a cone hit loses the chain |
| **Jump** | airtime and distance past the lip, read off painted 5 m lines |
| **Smash alley** · **Burnout box** | props smashed; seconds of held burnout and the hottest tyre |

Station names are painted on the road and on double-sided signs with lit letters (a 5 × 7 block font covering
A–Z and 0–9).

**Cones** (`Source/ConeSequence.js`): 136 of a new 70 cm cone — a fluorescent PVC body with a rounded tip and two
retroreflective collars on a chamfered rubber base, weighted low (3.2 kg, centre of mass 15 cm up) so it rocks
before it topples. A cone at rest is a *sleeping* rigid body and a static occupant (in the distance field, carded,
relit round-robin), so they cost nothing until the car arrives; a knocked cone follows its body every frame until it
settles. Reset (T, or a drill start) bursts it into cubes where it lies and assembles it on its mark.

## Smoke and fire

The thermal solver still computes tread/carcass/gas temperatures and ignition. The
rendering and transport are copied from Slate `3c987f3`: `PlumeSpace.js`,
`PlumeSourceProjection.js` and all five `Plume*.wgsl` files. `SmokeSequence.js` is now
an adapter for existing telemetry, presets and test/burst emitters, not the old gas solver.

Sliding contact energy and slip speed gate smooth patch and wheel-arch emissions.
Tread speed injects rearward/swirl momentum; airflow carries it away. Fine toroidal
smoke/heat/flame/soot volumes follow coarse buoyant incompressible flow, pressure
projection and vorticity confinement. The ray march includes sun/ambient/fire lighting,
self-shadowing and depth clipping, and composites **before TAA**. Quality follows the
host grade (Low/Medium/High). Empty fields are dormant; residual smoke has a 12-second
retirement ceiling. Shredded tyres stop producing rubber smoke.

The Smoke + Fire editor exposes supported emission, flow, rendering, light and fire
parameters, pressure iterations, presets and a test emitter. Legacy erosion/noise
knobs which have no mapping to the Slate backend are not displayed. The adapter
preserves the existing preset schema, but unsupported legacy fields have no effect.

## Burnouts and tyre marks

Holding W and S together at a crawl engages **line lock** (`VehicleSolver.js`): only the front calipers clamp, at
1.6 × the pedal's torque, and a clamped tyre's static-friction margin (about a third above the sliding value) holds
the car; the rears spin against them on the limiter with the gear held. The tyres heat from their real sliding work,
smoke pours off them and — held long enough — they ignite. Lift S and the car launches.

**Rubber** (`Source/TyreMarkDepot.js`): each texel of a 4096 × 2340 deposit over the whole pad (5.5 cm) gains the
sliding work per unit area a tyre does on it (60 kJ/m² is black). A burnout blackens its patch in a moment; a drift
leaves grey arcs that darken pass after pass. A second channel records where across the tread each texel lies; the
surface raster filters it among touched texels only, which puts every mark's edge exactly where the tread's was
(antialiased over one pixel, far sharper than the texels) and cuts the tread's four grooves into it — along the
path, as real marks are. The CPU copy is sparse (64² tiles where rubber has been laid). A road pixel becomes the
rubber family with probability equal to the deposit and TAA resolves that into a smooth blend — no lighting program
needed to change. Wheels riding on debris lay nothing.

## Tyre pressure and volumetric deformation (GPU Chaos Flesh physics)

`Source/VehicleVisualState.js`, `Shader/VehicleVisual.wgsl`, `Source/VehicleSolver.js`,
`Source/TyreThermalSolver.js`. GPU-accelerated volumetric tyre physics inspired by
Unreal Engine's Chaos Flesh volumetric / PB-FEM deformation continuum:

* **In the dynamics** each tyre is a spring in series with its strut, K = 25 kN/m (sidewalls) + 0.9 N/m per Pa (the
  air column) — 223 kN/m at 220 kPa, a 14 mm static deflection; 25 kN/m with no air (a single flat sits ~64 mm down,
  the car 14 mm lower on that corner). Each axle's two strut–tyre stacks are solved together with the anti-roll bar
  between them (bisection, three Gauss–Seidel passes): the bar pushes back on each side by k·(own − partner)
  compression — on the right exactly as on the left — so a corner's tyre deflection carries exactly the load it
  bears and a flat corner sinks onto its tyre. A wheel with nothing pushing on it (spring off its seat, tyre not yet
  touching) hangs down to the road or to full droop rather than hovering. Past the pinch the rim flange stiffens the
  stack; a topped-out corner rests on the tyre's own damping. Grip falls with pressure (0.62 + 0.38·(p / 0.75 p₀)^0.7,
  × 0.7 pinched), rolling resistance rises (up to ×4 flat), the rolling radius shrinks with the deflection.
  `node Tools/VehicleStability.mjs` holds the solvers to it without a GPU in a few seconds: rest at 220 and 60 kPa
  and on one, two and four flats (the body still), blowouts on each corner (left and right mirror each other), blowouts
  and shreds at 20–30 m/s and a weave on soft and flat tyres (upright, never airborne), donuts both ways (each
  corner's sliding work mirrors its partner's) and coasting backwards on a bare rim (the AMT's clutch opens when the
  wheels turn against the gear, so engine braking no longer spins them up).
* **On the GPU** (`Shader/VehicleVisual.wgsl`), the vertex stage evaluates real-time volumetric tyre continuum
  physics across all render passes (world raster, directional sun shadow, forward pass, and mirrors). Under contact,
  the carcass flattens against the ground plane; volume conservation (incompressible hyperelastic rubber & trapped air)
  distends the sidewalls laterally (Poisson bulge), displaces shoulder waves fore/aft of the patch, shears across
  lateral slip angles, and adapts its contact span dynamically to internal gas gauge pressure ($P_{norm} = P / 220\text{ kPa}$).
  Shredded rubber facets collapse cleanly to the axle, exposing the rim.
* **Pressure** is the gas law of the thermal model — air mass, gas temperature, leaks: set cold pressures per axle,
  or from the HMI's Tyres app. **Failure**: sliding work abrades the 1.1 kg tread (faster when hot); a tyre bursts
  when its tread is worn through, its carcass cooks past 220 °C or hot gas drives it past 650 kPa — a blowout empties
  it in a fraction of a second, with rubber chunks thrown from the wheel and a puff of smoke. A hard pinch cuts the
  carcass (a slow leak); the buttons puncture, blow out or shred any tyre. A flat rolled 150 m sheds its rubber: the
* **Pressure** is the gas law of the thermal model — air mass, gas temperature, leaks: set cold pressures per axle,
  or from the HMI's Tyres app. **Failure**: sliding work abrades the 1.1 kg tread (faster when hot); a tyre bursts
  when its tread is worn through, its carcass cooks past 220 °C or hot gas drives it past 650 kPa — a blowout empties
  it in a fraction of a second, with rubber chunks thrown from the wheel and a puff of smoke. A hard pinch cuts the
  carcass (a slow leak); the buttons puncture, blow out or shred any tyre. A flat rolled 150 m sheds its rubber: the
  wheel runs on its rim (radius 0.357 m, a third of the grip) and grinds sparks. The rez brings fresh tyres.

## Sparks and the body's hull

`Source/SparkSequence.js`, `Shader/SparkRaster.wgsl`, `Source/HullStructure.js`. The body no longer meets the road
and the boxes as seventeen 0.32 m spheres: ~700 points ray-cast from its own facets (underside, top, flanks, nose,
tail, mirrors, every crease vertex) are projected out of the course and the obstacle boxes, so the car sits on what
it actually touches — no gaps for a crate to nose between, no resting on air above a kerb. Every point sliding faster
than 2 m/s scrapes, and so does a rim on the road (pinched through a flat, or bare). Each scrape throws sparks at
1900–3000 K: a hull point's leave along the slide, fanned and kicked off the surface; a rim's leave its edge a little
behind the patch, where the metal has just left the road, at that point's own velocity (slip + S(1 − cos α)·F +
|S| sin α·N, S = Spin·r) — up off the trailing edge, streaming back from a rolling wheel, forward along the slide
from a locked one, back off a spinning one. They fall, cool radiatively and collide with the road, static world boxes and convex debris, drawn after the
antialiasing as thin additive blackbody streaks (2.2 mm, at least a pixel) from where the camera saw them a frame ago
to where it sees them now, depth-tested against the scene.

## The cabin and the HMI

`Source/SlateCabinLayout.js`, `Source/SlateCabinFittings.js`, `Source/CabinStructure.js`,
`Source/CabinSequence.js`, `Shader/CabinOverlay.wgsl`, `Shader/ForwardRaster.wgsl` and
`Source/Cabin/`. The Slate dashboard, centre console and seats are procedural facets.
The 64×10 toroidal steering grip, spokes, **wheel-mounted cluster** and its screen all
turn together with steering ×3. The tablet keeps the reference placement/tilt and
30 × 18.75 cm physical screen. Host glass and three mirrors are retained.

* **The HMI** is `Experimental/TabletPanel` (arena/215a6f39-frontier) as is — launcher, twelve applications, five
  launcher designs, the hexagon cluster — drawn into its own canvases and uploaded as textures when they redraw. The
  cluster shows the car's speed, revs, gear and tyre pressures; Telemetry and Tyres read the solver; the Tyres slider
  pumps the tyres; the **Garage shows this car**, rendered live into its preview well by the exhibit's own studio pass
  (its own camera and three lights, on a turntable you can drag). From the cockpit, click the tablet.
* **Glass** is see-through: the glazing leaves the opaque raster and the cabin overlay draws it over what is behind —
  the sky's Fresnel reflection, a sun glint, 72 % transmission; it casts no shadow, so the sun reaches the cabin. The
  cabin's traced indirect light is attenuated (`Shader/CabinVolume.wgsl`): its surfaces lie inside the body's
  distance volume and would otherwise gather the sunlit exterior.
* **Mirrors** are a separate render pass: the driver's eye reflected in each mirror's plane, through an off-axis
  frustum fitted to its glass (Kooima), its near plane on the glass, a small lit forward pass over the same facets —
  one mirror per frame while the mirrors can be seen.
* **HUD size/position** (Cabin · HMI): scale 0.6–1.8 and horizontal/vertical offsets
  ±200 CSS pixels. This changes only `#hud`, not any mesh, screen, touch plane or eye.
  The tablet has its own separate HMI scale control (above); `?hudscale=1.2` sets the HUD initial scale; obsolete `?cockpit=` sizing is ignored.
* **Cameras**: C cycles chase, cockpit, passenger, hood and two tyre rigs (also in Vehicle → Camera and Cabin · HMI).

## Fracture

Breakable props (`Source/FractureSequence.js`) are closed convex solids with Fracture-editor recipes. The car's hull
is tested against them a frame ahead; the impact energy is ½·μ·vₙ² (μ the reduced mass of car and prop). Below the
material's threshold the prop is a collider and stops the car; above it the prop fractures with
`Source/FractureStructure.js` — the Fracture Editor's material-biased, energy-limited plane partitioning: fragment
area is bought at 2·Gc per square metre (glass 7, concrete 140, wood 320, ABS 460 J/m²), tempered glass adds its
stored stress and dices, wood splits along the grain, panes crack radially, slivers and needles are refused, and
volume is conserved to 1e-7. **Dynamic** mode cuts at the true impact point with the impact's share of energy (a
faster hit buys more pieces; one on-the-spot fracture per frame, ~5–15 ms); **Baked** uses the stored pattern.

Fragments are written into the prop's composite facet set and drawn in **one call per prop** (each vertex names its
fragment occupant) and cast shadows. The car pays the momentum of an inelastic exchange. The debris **stays where it
lands** — the prop is rebuilt by T, *Reassemble*, or (with *Respawn* switched on) after the lifetime.

**Debris physics** (`Source/RigidBodyWorld.js`, shared with the cones): every fragment is a convex hull — its points
and its face planes, about its centre of mass — at 120 Hz:

- against the course: hull points below the analytic surface, along its normal;
- against the walls, barriers, containers and intact props: hull points inside a box leave through its nearest face,
  box corners inside a hull push the hull off;
- against each other and the cones: each hull's contact samples (corners, edge midpoints, face centroids) inside the
  other's planes, through a uniform grid, a separating-plane early-out, and a spread six-contact manifold;
- against the car, **both ways**: the hull spheres and four wheels against each hull's planes, with the car's mass
  and inertia — a cone flies, a concrete slab stops the car;
- under the car: the strut casts read the highest hull top under each tyre, so the car **drives over** debris (steps
  up to 20 cm; anything taller is an obstacle), and the tyre's load and traction pin or drag the piece it rides.

Sequential impulses with restitution only for real approaches, Coulomb friction, a split projection that sees the
penetration already removed, and sleeping: a settled body is immovable until something with momentum reaches it.

**Fragments in the distance field**: every fragment large enough for the field to resolve (radius ≥ 0.75 of the
finest cascade's voxel) gets a 12³ signed distance volume of its own in a 256-slot pool appended to the occupant
distance space, computed on the device the frame it is born (`Shader/FragmentDistance.wgsl`: inside, the largest face
plane distance; outside, the nearest triangle — both exact for a convex solid). It joins the union's active list
like any occupant, so the debris occludes, shadows and bounces light in the GI the frame the prop breaks. Fragments
own no surface cards; a traced ray that lands on one shades it from its packed albedo, the sun (a short traced
shadow) and the sky (the card-less fallback in `Shader/CardSampling.wgsl`).

The union no longer walks a span of occupants with inactive ones carrying empty bounds: it walks an **active list**
uploaded when activity changes, so sleeping fragment slots and disabled stress slots cost nothing.

## Derez and recovery

`Source/EffectSequence.js`, `Shader/EffectRecords.wgsl`, `Shader/EffectRaster.wgsl`, `Source/RecoveryDirector.js`.
The Particle Editor's derez presets, in the renderer: one **release clock** decides everything. A point of the car
belongs to a cell of a 16³ lattice over its bounds, whose release time is a wave across the bounds plus a hashed
jitter. The surface raster glows a car cell white-hot, then cyan, and drops it the instant its cell is released;
the effect raster launches a cube from every anchor of that cell (1 920 anchors spread over the car's facets) —
drawn into the surface projection, so the cubes are lit, glow and bounce light in the GI. Each cube's whole flight
is closed-form (gravity and linear drag solved exactly, a floor impact found by bisection, eight children that
tumble out and shrink): nothing is simulated or read back, and an emitter is 128 bytes of uniform and one instanced
draw. The car's shadow comes apart with it (a masked shadow program for its runs). Rez runs the clock backwards:
cubes fly in from a shell and each cell appears as its cubes arrive. Cones use a box emitter.

**The car's own colours**: every anchor carries the material family of the facet it was spread on, so the lime coat
comes away as lime cubes, the glass as glass, the tyres as rubber, the lamps as lamps — and the falling blocks and
their shards keep them. Cells about to go, and cubes as they leave, glow in that colour (a G-buffer glow channel the
recombination adds as albedo × glow). The palette look remains a click away. **Momentum**: the car keeps moving while
it comes apart — it coasts under the solver with nobody at the controls — and its pose is sampled at 60 Hz, so each
cube leaves from where the car is when its cell lets go, with the car's velocity (and spin) there.

**Recovery**: an overturned car (1.5 s), a car airborne 2.5 s or tumbling for 0.9 s, or one 35 m outside the grounds
derezzes, vanishes from the raster, the field and the cards, is moved — upright where it lay, or to the last safe
spot on the pad — and rezzes in on fresh tyres, held still until it is complete. R and every teleport use the same
sequence. The VFX page edits the look (presets, timing, burst, speeds, drag, life, size, style, face colours or
palette, glow, momentum) and the triggers.

## URL parameters

| Parameter | Meaning |
| --- | --- |
| `quality=low\|medium\|high` | cascade resolution, card density, traversal length, render scale (default `high`) |
| `scale=0.25…2` | override the render scale |
| `preset=Noon\|Golden\|Dusk\|Night` | lighting preset at start |
| `stress=N` | enable N stress objects at start |
| `stressmotion=0\|1\|2` | stress objects static, one in ten moving, all moving |
| `stresspool=K` | reserve K stress objects at bring-up (default 1536, clamped to the identity ceiling) |
| `inspection=0…9` | start in a debug display |
| `omit=Union,Cards,Shadow,Direct,Resampling,Diffuse,Reflection,Inspection,Resolve,Present,Smoke` | diagnostic: leave stages out |
| `camera=Chase\|Cockpit\|Passenger\|Hood\|Tyre FL\|Tyre RL` | camera rig at start |
| `hudscale=0.6…1.8` | on-screen diagnostic HUD scale (physical cockpit stays fixed) |
| `capture=1` | diagnostic: present through a read-back canvas (headless software adapters) |
| `renderer=bvh\|sdf` | the BVH path tracer (default) or the distance-field renderer (`rtx` and `gtx` still work as the old names) |
| `screentraces=1` | screen traces first (nearest-depth hierarchy walk), then the distance field (SDF mode) or the BVH tracer (BVH mode) |
| `lightgrid=0` | test every light per pixel instead of the world light grid (an exact cull: the picture must not change) |
| `gridreach=0.05…1` | diagnostic: scale every light's reach in the light grid cull. 1 is exact; below 1 deliberately drops light, so the tests can prove they would notice |
| `ies=0` | analytic beam shapes instead of the IES photometric profiles |
| `emissive=0` | no emissive-surface light sampling (BVH renderer): emitters are found by BSDF hits only |
| `tyres=analytic` | the analytic tyre carcass spring instead of the XPBD carcass |
| `tyresubsteps=8…32` | XPBD substeps per frame (default 32; 16 is not converged) |
| `bounces=1…4`, `reflbounces=1…4`, `candidates=1…4` | BVH path tracer: GI bounces (3), reflection bounces (2), initial RIS candidates (2) |

## Stress test

The *Stress test* inspector adds 64 / 256 / 1024 objects, fills or clears the pool, and sets their motion. The pool
(crates, pillars, drums, balls; one in eleven emissive) is built with the scene and scattered over the free pad and
the grass beyond, so enabling objects costs no construction: the union pass walks a longer span, the draw runs grow,
the cards join the relighting lists, the vehicle gains colliders and the bricks under the objects are rewritten once.
The layout is seeded, so `?quality=high&stress=1024&stressmotion=1` reproduces the same scene on every machine. The
Frame timing card shows the average frame rate, the **1 % low** over the last 240 frames and per-pass GPU timings
(where `timestamp-query` is available).

## Headless validation

`Tools/HeadlessHarness.mjs` runs the real bring-up and frame loop — editor included — under Node with Dawn (the
`webgpu` package): on the null backend it validates every WGSL module, pipeline layout and command; with a
SwiftShader Vulkan ICD it renders real frames and writes PNG captures. A `HOOKS` module can script input (the
burnout, a run down the smash alley) and report from the host. See the file header for usage.
`Tools/VehicleStability.mjs` needs nothing installed: it steps the vehicle, thermal and carcass solvers in the frame
loop's order on the open pad and fails on a bouncing body, a left–right asymmetry, a car leaving the ground or a
runaway wheel (see *Soft tyres, pressure and failure*).

## Licences of bundled assets

`Style/Fonts/` — DM Sans, SIL Open Font License 1.1 (`Style/Fonts/OFL.txt`). `Source/EditorIcons.js` — lucide
path data, ISC License (notice in the file).

## Implemented effects and steering follow-up

- **Road marks:** RG stores rubber deposit/lateral coordinate; BA stores separate rim scrape data.
  Shredded contacts lay two narrow flange scratches (6 cm each), not a rubber skid.
  RimScrape is an abraded-aggregate/metal-transfer material, not black rubber. Contact-kind
  transitions reset the trail; debris-supported and disabled contacts lay nothing.
- **Sparks:** swept segment collision at up to 120 Hz against the course, static collider
  boxes and current convex debris hulls. Bounces lose normal/tangential energy and heat;
  settled/cold particles retire. Blackbody chromaticity and size-dependent cooling fade
  smoothly to red; reduced HDR energy avoids the previous overbright yellow clipping.
- **Lighting:** at most eight transient proxy lights shared across spark clusters and
  derez/rez emitters, with per-cluster and whole-pool energy limits, distance falloff,
  distance-field shadows in screen/card direct lighting, and decay to zero. Card lighting
  feeds GI. Mirrors use unshadowed proxy lights. Proxies approximate the bulk of the effect,
  not individual particle lights; the card/temporal caches retain their normal update lag.
- **Countersteer:** Vehicle → Telemetry exposes an on/off switch and strength (default 0.45).
  It steers toward lateral travel and damps yaw, fades in from 3–12 m/s, yields at manual
  lock, and disables in reverse/airborne. Scripted input bypasses it for deterministic tests.

Loose bodies use browser/WASM Jolt with a custom-vehicle chassis bridge (see [SLATE_PORT.md](SLATE_PORT.md)). Visual quality/exposure and feel still need a real
WebGPU-browser driving review; Dawn null-backend validation does not render pixels.

## Driving camera effects and overlay binding compatibility

**Vehicle → Camera** has switches for driving shake and speed-based FOV, plus shake
strength (0–1) and maximum FOV boost (0–24°). Defaults are gentle shake and a 14°
boost, smoothed with speed up to 45 m/s. Acceleration/landing jolts add bounded
transient vibration. Chase, cockpit, passenger and hood cameras participate;
HMI touch focus, tyre inspection and free camera remain unchanged. Disable either
effect independently for comfort.

The Cabin overlay schema explicitly includes exposure storage binding 12 and has
regression coverage against the shader declaration. The entry-point/cabin imports
are versioned to avoid mixing old JavaScript layouts with new WGSL. `npm run serve`
now uses `Tools/Serve.py`, which sends no-store headers for preview resources. If
an old error page is still open, reload the preview once to load the updated modules.

Validation for this change: 30 tests pass; 36-frame effects and 48-frame active
plume/HMI runs report zero errors and warnings. Dawn null validation does not prove
rendered appearance or subjective camera comfort.

## Independent vehicle orbit camera

Choose **Vehicle → Camera → Orbit**, use the **Orbit vehicle** button, or cycle to
Orbit with **C** / **Shift+C**. Drag the viewport to rotate through 360° around the
vehicle and scroll to zoom (4.5–40 m). Vertical angle is bounded to avoid a flipped
camera, and the eye stays above the analytic course. **Reset orbit** restores the
initial view. Rotation/zoom persist when switching away and back.

Unlike Chase, Orbit keeps world-space angles: it stays centred as the car moves,
but does not swing behind it when it turns or automatically recenter after dragging.
Driving shake and speed FOV do not affect Orbit. This is a vehicle inspection
camera, not free-flight, and does not provide arbitrary-obstacle camera collision.

Validation: 33 unit tests pass; a 30-frame Orbit integration run exercises rotation,
zoom and persistent angles with zero GPU errors/warnings (Dawn null backend, not a
rendered visual review).

## Spark variation

Sparks now mix mostly fine filings (72%), medium fragments (22%) and occasional
heavier chips (6%). Each keeps its own temperature, size, drag, cooling, lifetime,
bounce retention, intensity, width and shutter fraction. Fine filings disappear
quickly; heavier orange chips travel/cool differently. Contact emission arrives in
short irregular packets rather than a constant stream. Tapered streaks have varied
length/width and subtle age-driven brightness modulation, without re-rolling every
render frame or expanding the previous thin-particle footprint cap.

**Vehicle → Spark variation** controls the added variation (default 1, applied to
new particles). Swept collisions, monotonic thermal cooling, particle limits and
bounded pooled lighting remain; visual and proxy-light intensity share the same
per-particle gain. No additional light system or GPU bindings were introduced.

Validation: 37 tests pass, including seeded reproducibility, fragment proportions,
property diversity, irregular emission counts, pulse cleanup, cooling/retirement
and lifetime fade. Populated 36-frame effects validation reports no GPU errors or
warnings. Appearance still needs real-browser review.
