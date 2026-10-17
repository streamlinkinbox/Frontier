# WebGPU triangle ReSTIR GI / path tracing

## What changed

The existing Vehicle HMI Prototype now defaults to **`renderer=restir`**. This is
integrated in the same scene, game loop, vehicle physics, Jolt/WASM world, cockpit,
Garage, training arena, smoke, tyre marks and fracture system. It is not another
scene or a native C++ executable. The supplied `70747de` baseline is the parent of
this implementation; the original supplied baseline branch is not overwritten.

| Backend | URL | Transport |
| --- | --- | --- |
| Default triangle ReSTIR GI | `?renderer=restir` (or no renderer parameter) | Triangle camera rays, first-connection resampling, replayed multi-bounce paths, optional SVGF/TAA |
| Independent reference PT | `?renderer=reference` | Ordinary BSDF path sampling + NEE/MIS; no reservoir reuse, no SVGF, TAA off initially |
| Existing GTX GI | `?renderer=distance` | Retained distance-field traversal, surface-card radiosity, legacy ReSTIR gather/reflections |

**RTX is not a standard WebGPU capability.** The new renderer intersects triangles
using WGSL compute and BLAS/TLAS buffers. Browser WebGPU does not expose a standard
RTX/DXR/Vulkan-ray-query interface. An RTX GPU can execute this code, but the code
does not use its RT cores. There is no vendor-name detection pretending otherwise,
no DLSS integration, and no hardware frame-rate claim. Both renderers require a
working WebGPU adapter. Unsupported devices show an error instead of silently
switching the requested renderer.

## Run and control

```sh
cd Experimental/VehicleHmiPrototype
npm ci
npm run serve
# http://localhost:8080/   (HTTPS is required on a remote host)
```

The server binds `0.0.0.0:8080` and disables stale JS/WGSL caching. Browser requests
use relative paths; no separate local backend or CDN is required.

Choose **Render settings → Rendering backend** to switch modes. The selection
updates the URL and reloads deliberately, releasing the old renderer's resources.
The renderer card also exposes path vertices, fresh candidates, Russian-roulette
start, wavelet iterations and RT target-memory budget. Reconnection visibility and
suffix replay cannot be disabled in ray-traced modes.

Example reproducible URLs:

```text
/?renderer=restir&quality=low&trace=4&bounces=4&candidates=1&seed=173
/?renderer=restir&trace=2&bounces=6&candidates=2
/?renderer=reference&scale=0.5&trace=1&bounces=16&candidates=8&seed=173
/?renderer=distance
```

- `quality=low|medium|high`; default high.
- `trace=1|2|4|8`: screen pixels per trace pixel on each axis. Default half
  resolution; reference requests full resolution initially. Memory guards can
  increase this divisor and show the actual trace size in the inspector.
- `scale=0.25…2`: display render scale, subject to the existing quality pixel cap.
- `bounces=2…16`: **maximum path vertices**, including the primary vertex; default
  six (four on low). This is a finite path-depth budget, not unlimited transport.
- `candidates=1…8`: fresh paths/pixel/frame; default one.
- `seed`: reproducible unsigned integer random seed; default 173.
- Existing `preset`, `camera`, `stresspool`, `stress`, `stressmotion`, `capture` and
  diagnostic `omit` parameters remain available. Do not use omitted passes to
  claim normal rendering performance.

Reference mode uses the same geometry/material integrator but independent ordinary
BSDF sampling, not a ReSTIR reservoir with reuse merely switched off. It is an
unfiltered per-frame comparison, not an offline accumulated-image viewer. Increase
candidates and average linear raw frames for convergence comparisons. Restore TAA
to off if you enable it manually before comparing.

## Implementation

### Actual scene triangles and dynamic acceleration

`Source/RayTracingScene.js` packs 32-byte threaded SAH nodes, 96-byte triangles,
instance descriptors, instanced-triangle sampling domains, emissive-triangle
probabilities and bottom-up refit schedules. The CPU builds binned-SAH BLAS/TLAS
structures; shaders perform all ray traversal and transport. Shared static meshes
have one BLAS. The articulated vehicle body and four wheels have separate deforming
BLASes. Unused vehicle-template meshes are not duplicated as static BLASes.

- Stackless preorder escape links terminate traversal; no fixed ray-step or stack
  cutoff hides otherwise valid geometry.
- Watertight shear-space triangle tests include a floating-point forward-error
  bound. Scale-aware ULP ray offsets and explicit originating-triangle exclusion
  prevent self-intersection, including large planar facets near the world origin.
- Instance-local ray directions are **not renormalized**, preserving world `t`
  under nonuniform and negative scales.
- `PathRefit.wgsl` uses the exact shared `VehicleVisual.wgsl` deformation functions,
  including tyre deflection/pressure, shredding, cabin articulation and HMI scale.
  Per-level dispatch boundaries refit BLAS then TLAS; there is no inter-workgroup
  spin-wait/deadlock scheme.
- Previous GPU-deformed vehicle vertices are retained for motion reprojection;
  previous rigid transforms alone are insufficient for changing tyre/contact or
  tablet shape. Camera jitter deltas and trace/sample-coordinate conventions are
  also included in temporal reprojection.
- Fracture slots retain their actual local triangle geometry and get real BLAS
  instances. Composite raster batch geometry is not substituted for every shard.
  Inactive occupants and card-only ground tiles are not ray geometry.
- Topology, activity, facet set, material override and emission changes rebuild the
  packed scene and restart history. A periodic 120-frame SAH repack bounds refit
  degradation. Static local SAH trees are cached by facet-array identity. Previous
  deformed vertices survive a periodic repack that does not change topology.

### Transport, next-event estimation and MIS

`Shader/PathTransport.wgsl` integrates diffuse, GGX visible-normal glossy/metallic,
clear-coat and thin-sheet coloured transmission with the game's material records.
It samples BSDF lobes, compensates lobe probabilities, traces subsequent surfaces,
uses throughput-compensated Russian roulette and applies geometric-side tests.

NEE samples the sky, finite sun disk, power/area-weighted emissive triangles and
existing finite automotive/street/effect/fire lights. Continuous emission and
escaped environment paths use power-heuristic MIS against NEE. Tinted shadow rays
follow intervening thin sheets; delta transmission carries its selection mass.
The primary transmission branch is handled separately from continuous GI samples.

Light PDFs are checked before divisions, including zero-length emitter links.
MIS is evaluated with normalized densities to avoid squared-PDF overflow. Purely
diffuse fixtures with zero specular/metalness have no artificial grazing Fresnel.
There is no ambient constant, surface-card lookup, split-sum reflection response or
legacy cabin attenuation term standing in for path transport.

### Reservoir domain and normalization

`Shader/PathResampling.wgsl` stores a canonical sample:

```text
(instance, triangle, uniform-area barycentrics, suffix random seed)
OR (environment direction, suffix seed)
```

The multi-bounce suffix is **replayed at the destination outgoing direction**,
including its non-Lambertian BSDFs, lights, visibility and Russian roulette. It is
not a cached view-independent colour at the second vertex.

Fresh proposals mix 80% BSDF sampling with 20% defensive canonical sampling. The
latter samples instanced triangles uniformly (75%) or the sphere (25%). Canonical
triangle area-fraction measure uses the reconnection factor

```text
J(x,y) = worldArea(y) * abs(dot(n_y, -direction(x,y))) / distance(x,y)^2
```

in **both the integrand and directional proposal**. Current world area, rather
than an ad-hoc clamped distance ratio, handles scale/deformation changes. The
importance target is luminance with a positive defensive-density/light-scaled
support floor. The floor is for sampling only: it adds no emitted/ambient light.
Black and occluded samples remain part of the common support.

For a reused source reservoir:

```text
w = target_destination(sample) * W_source * M_source
W_destination = sum(w) / (M_destination * target_destination(selected))
```

Temporal capping rescales source multiplicity and weight together. Compatibility
uses primary surface guides, not the selected sample's visibility. Black/occluded
candidates still count in `M`; there is no visibility-conditioned valid-count
normalization. Each reconnection rechecks triangles. Spatial reads use separate
scratch/history buffers, and the **final spatially resampled reservoir** becomes
next frame's temporal history.

This is **ReSTIR GI connection resampling with a multi-bounce path tracer**. It is
not a claim to implement arbitrary all-vertex ReSTIR PT path-space shifts, hybrid
shift mappings, bidirectional transport or an NVIDIA native SDK.

### Reconstruction and integration

`PathDenoise.wgsl` stores float32 luminance moments and reprojects traced guides.
`PathWavelet.wgsl` applies edge-aware 5×5 à-trous filtering, with a 3×3 spatial
variance bootstrap when temporal history is short. Without this bootstrap a single
sample has zero temporal variance and the filter incorrectly rejects neighbours.
Point-to-plane edge weights preserve filtering on a receding flat road instead of
mistaking its perspective depth gradient for a geometry boundary. Raw debug mode
also disables display TAA/jitter so it is genuinely unfiltered.
Only reconstruction history is neighbourhood-clipped; raw radiance and reservoir
weights have **no firefly/radiance clamp**. `PathResolve.wgsl` reconstructs to the
screen with opaque raster guides, without rejecting traced glass in favour of the
opaque object behind it. Float16 saturation occurs only at the display attachment;
raw transport/moments remain float32.

The display consumes complete camera-path radiance instead of adding legacy direct
or glossy light a second time. Forward world glass/mirror overlays are suppressed
in this mode, but live tablet/cluster interaction and Garage rendering remain.
Smoke/plume is composed once. Distance union updates and card relighting are not
run for path transport; the distance field wakes and rebuilds when smoke or its
debug view needs it. Legacy screen direct lighting runs only when particle VFX
needs its existing hybrid shading. Existing physics/gameplay code is not replaced.

## Memory and failure behaviour

Per trace pixel: three 144-byte pixel/reservoir/guide buffers and eight 16-byte HDR
textures, **560 bytes** total. Add a full-screen 8-byte/pixel float16 resolve target.
The default **384 MiB RT-target budget** accounts for both terms and for individual
`maxBufferSize`/`maxStorageBufferBindingSize` limits. Display dimensions also respect
`maxTextureDimension2D`. It is not a total VRAM budget:
geometry, BVH/base/previous vehicle triangles, raster guides, the retained game
systems, smoke, shadow resources and display history also consume memory.

For example, 1920×1080 at half trace resolution needs about 293 MiB of these RT
targets, excluding other game resources. At full trace resolution it exceeds the
default target budget. Resize destroys retired resources, updates dependent bind
groups and restarts histories. In-flight capture buffers are retired safely rather
than leaking or permanently wedging the capture-pending flag. Geometry has its own
storage-binding size guard.

A fatal WebGPU/pipeline/device/submission error stops rendering and offers an
explicit distance-renderer reload link. There is no silent capability downgrade
and no automatic device-loss resource recreation; a reload is required.

## Validation

```sh
npm ci
npm --prefix Tools ci
npm test                         # CPU/unit + existing gameplay checks; GPU fixture skips
npm run validate:restir           # 12-frame dynamic integration, null backend by default
npm run validate:reference
npm run validate:distance
npm --prefix Tools run test:stability
npm --prefix Tools run validate:effects
npm --prefix Tools run validate:slate
npm --prefix Tools run validate:orbit

# EXECUTING adapter, not the null command validator:
BACKEND=vulkan npm run test:gpu
BACKEND=vulkan SIZE=320x180 HARNESS_TIMEOUT=600000 \
  HOOKS=Tools/PathTracingHooks.mjs OUT=/path/to/rt-check.png \
  node Tools/HeadlessHarness.mjs . 10 \
  '?capture=1&quality=low&scale=1&stresspool=8&trace=4&bounces=4'
```

Vulkan needs an installed working ICD. SwiftShader can execute these checks on a
CPU via `VK_ICD_FILENAMES`/`LD_LIBRARY_PATH`; no software driver or dependency tree is
checked into the repository. GPU-test timeout is ten minutes to allow software
shader compilation. Compilation/setup-dominated harness duration is **not game
FPS**. CPU tests and Dawn's null backend alone do not prove rendering correctness.

`Tools/PathTracing.test.mjs` covers defaults, byte layouts, full-resolution memory
costs, BVH-vs-brute-force traversal, nonuniform/negative transforms, canonical
reconnection and statistically normalized RIS with differing source proposals,
black visibility and capped temporal/spatial reuse. `PathEstimatorReference.mjs`
is an independent finite-integral estimator oracle, not a renderer fallback.

`Tools/PathTracingGpu.test.mjs` executes actual shaders against 64×64 analytical
fixtures, reads unclamped float32 radiance and checks both modes:

- Constant-white environment on a Lambert plane, expected `[0.7, 0.4, 0.2]`.
- Two large emissive diffuse planes, 16 vertices, expected per-channel bottom
  radiance `A*E/(1-A*B)`. This caught a real numerical self-intersection/NaN-MIS
  explosion that finite-colour/command validation alone had missed.
- A metallic/clear-coated cavity, including a glossy second vertex, compared
  against independent PT to exercise view-dependent suffix replay.
- Coloured thin glass over the diffuse plane under white sky: expected
  `albedo * tint^2`, accounting for both illumination and camera transmission.
  Zero-specular reflection/proposal/transmission Fresnel conventions agree.
- First-history SVGF variance reduction and mean preservation on front-facing and
  tilted planes, using the actual filtered textures instead of dispatch validity.

A software-Vulkan run after the numerical fixes produced:

| Fixture | Expected | Reference PT | ReSTIR GI |
| --- | --- | --- | --- |
| Environment RGB | 0.7000, 0.4000, 0.2000 | 0.7006, 0.4003, 0.2002 | 0.6963, 0.3979, 0.1989 |
| Diffuse cavity RGB | 0.8861, 0.2500, 0.05952 | 0.8849, 0.2496, 0.05943 | 0.8860, 0.2500, 0.05951 |
| GGX/coated cavity RGB | Independent PT comparison | 0.7995, 0.2452, 0.06605 | 0.8097, 0.2479, 0.06679 |
| Thin-glass plane RGB | 0.5721, 0.3534, 0.1952 | 0.5738, 0.3545, 0.1958 | 0.5738, 0.3545, 0.1958 |

Red-channel variance fell from 0.02643 (raw) to 0.000277 (filtered) in the flat-plane
fixture, and 0.02664 to 0.000276 on the tilted plane. This is a correctness/
reconstruction regression, not an all-material quality or performance guarantee.

`PathTracingHooks.mjs` exercises stress activation, tyre shredding, actual fracture
triangles, HMI/camera changes, resizing and reuse switches. On an executing adapter
it also reads reservoirs/deformed geometry, verifies collapsed rubber and retained
previous tablet deformation, and checks that triangle hits and reuse occurred.

Validation performed on 2026-10-17: 51 CPU/unit tests passed (the opt-in GPU suite
skips in the default run), the executing GPU suite passed separately, and all 23
vehicle-stability scenarios plus null-backend effects/Slate/orbit checks passed.
The latest executing 10-frame vehicle hook reported zero WebGPU errors/warnings,
10,669 primary hits, 8,584 reused reservoirs, 3,968 collapsed rubber triangles and
both tablet-housing facets retaining the previous deformation. A real Chromium 138 browser
on SwiftShader rendered the scene and scaled HMI through 25 frames with no app
WebGPU or JavaScript errors. The software-browser test used multiprocess GPU mode
and a disabled GPU watchdog; single-process/in-process-GPU flags lost the device
and are not a supported test configuration. Chromium emitted shared-image mailbox
messages during deliberate browser teardown. This does not constitute RTX testing.

## Explicit limitations / remaining production work

- **Not RTX-core accelerated** and **not arbitrary all-vertex ReSTIR PT**. These are
  fundamental distinctions, not options hidden behind an adapter vendor name.
- No hardware RTX/browser FPS or frame-time/memory-distribution benchmark has been
  collected. Compute traversal, repeated visibility/suffix replay and periodic CPU
  repacking are substantial costs. Real-time frame-budget qualification on target
  RTX hardware remains required; no 60 FPS/production-ready claim is made.
- Thin-sheet glass uses the existing transmission records. There is no solid IOR/
  thickness stack, dispersion, volumetric path transport, or full caustic solver.
- Smoke/plume, procedural spark/derez cubes, live displays, Garage and HUD remain
  hybrid game systems. Their existing light proxies can illuminate paths, but
  individual procedural particles and live screen texels are not secondary-ray
  scene geometry/light textures. Raster live-view mirror textures are not sampled
  by secondary rays (the underlying metal/glass material is traced).
- ReSTIR GI reuses correlated stochastic paths. The independent estimator and
  analytical fixtures validate specific cases, not all scenes, glossy convergence,
  multiple-sheet MIS, every moving-occluder regime or energy conservation of every
  authored artistic material. Low samples can still be noisy; SVGF is a biased
  reconstruction layer, which reference/raw modes deliberately bypass.
- No automatic device recovery, asset streaming/compaction pipeline, adaptive
  per-pixel sampling, spectral rendering or integrated upscaler SDK is claimed.
