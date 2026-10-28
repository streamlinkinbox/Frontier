# Fluid Path audit and Vulkan plan

## Source correction

The user identified the previously selected liquid node prototype as the wrong editor. The requested **Fluid Editor**
is now recovered at `Experimental/Fluid/` from
[`SultanAladin/Frontier-@f2cfc57d`](https://github.com/SultanAladin/Frontier-/tree/f2cfc57daaaf1fafc9a01a2002af6cad8cbd0f0f/Experimental/Fluid).
It is the gas-only WebGL2/WebGPU editor with source, collider, fire, smoke and shadow controls. Its own package names
its upstream as [`eosclient0001-rgb/Frontier@33c5f2e`](https://github.com/eosclient0001-rgb/Frontier/tree/33c5f2e13dc3d6912a8bb2d887a4e8061b401b6b).
The earlier `Experimental/FluidEditor/` is a different liquid node prototype, not the requested editor. **All earlier
liquid-node shader findings and measurements below apply only to that prototype**, not to `Experimental/Fluid/`.
The intended separate liquid editor remains unconfirmed; do not assume that node prototype or Flux is the right one.
A complete audit, parity verification, and production port of the recovered Fluid Editor still need to be done.

## Status

**Not production complete.** The browser sources are recovered and a small native grid-addressing slice is ported;
there is no Vulkan fluid solver, shared particle/force integration, scene shadow integration, or Windows build here.
This checkout began with only `README.md` and an unrelated patch; it did not contain the cited engine.

| Reference | Local entry | Pinned origin | What it actually is |
| --------- | ----------- | ------------- | ------------------- |
| Vehicle HMI | `Experimental/VehicleHmiPrototype/index.html` | [6afb6f00](https://github.com/SultanAladin/Frontier/tree/6afb6f00b5f25c45f7cda92c7da65b64845768f3/Experimental/VehicleHmiPrototype) | WebGPU driving scene with toroidal gas windows |
| Particle Editor | `Experimental/ParticleEditor/index.html` | [6afb6f00](https://github.com/SultanAladin/Frontier/tree/6afb6f00b5f25c45f7cda92c7da65b64845768f3/ReferenceApps/Working/ParticleEditor) | WebGPU particles, wind and force authoring |
| Fluid Editor | `Experimental/Fluid/index.html` | [f2cfc57d](https://github.com/SultanAladin/Frontier-/tree/f2cfc57daaaf1fafc9a01a2002af6cad8cbd0f0f/Experimental/Fluid) | Recovered gas editor with WebGL2 and WebGPU paths |
| Other liquid node prototype | `Experimental/FluidEditor/FluidNodeEditor.html` | [d1546196](https://github.com/SultanAladin/Frontier/tree/d15461969d6e5163da220eea85c6f561488f1011/Documentation/Prototypes/FluidNodeEditor) | Different FLIP/PIC liquid node prototype; not the requested editor |
| Volumetric Flow | `Experimental/VolumetricFlow/index.html` | [4da41d06](https://github.com/SultanAladin/Frontier/blob/4da41d06a8d3b06cdee86fca5f9023d697a1b247/Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html) | Standalone WebGPU gas/fire solver |
| Existing native water | Not copied | [28ec0657](https://github.com/SultanAladin/Frontier-/tree/28ec0657a23ed6bbf680f17cc06b464a53639dae/Projects/Project-Fluid) | Separate C++ PBF / pond and GPU surface extraction; not this FLIP or vehicle gas solver |

The previously imported `FluidNodeEditor` HTML in `arena/f8adb323-frontier` is a different prototype. The user-requested
`Experimental/Fluid` was located separately in `Frontier-`. Sources remain under `Experimental/`. The upstream lunar
atlas is required to
boot the Vehicle HMI; `PrepareReferences.py` restores it by pinned Git blob hash, outside the tracked source set.

## Launch and verify

```sh
python3 Experimental/FluidPath/PrepareReferences.py
python3 Experimental/FluidPath/ServeReferences.py
# Open http://localhost:8000/ or the hosted preview URL.
```

The recovered Fluid Editor defaults to WebGL2 and also offers an experimental WebGPU path. The Vehicle HMI requires
WebGPU and a secure context. HTTP is secure for localhost only; use HTTPS for remote access. The launch page links
the three requested references. The Vehicle HMI's embedded Particle Editor uses the same origin.
Flux and fracture workspace links in the recovered Vehicle HMI point to upstream references not included here.
The recovered editor and Particle Editor do **not** automatically exchange simulations or settings.

```sh
npm --prefix Experimental/Fluid ci
npm --prefix Experimental/Fluid test
npm --prefix Experimental/VehicleHmiPrototype test
npm --prefix Experimental/ParticleEditor ci
npm --prefix Experimental/ParticleEditor test
npm --prefix Experimental/VehicleHmiPrototype/Tools ci
SIZE=320x180 node Experimental/VehicleHmiPrototype/Tools/HeadlessHarness.mjs \
    Experimental/VehicleHmiPrototype 2 '?capture=1&quality=low&stresspool=0'
node Exhibits/FluidPath/Code/FluidShaderValidation.mjs
node Exhibits/FluidPath/Code/ProjectionValidation.mjs
node Exhibits/FluidPath/Code/PressureRoutingValidation.mjs
node Exhibits/FluidPath/Code/VolumetricSyntaxValidation.mjs
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    Engine/Fluid/SpatialGrid/CellWindow.cpp Exhibits/FluidPath/Code/CellWindowValidation.cpp -o /tmp/CellWindowValidation
node Exhibits/FluidPath/Code/CellWindowValidation.mjs /tmp/CellWindowValidation
```

Tests and limitations: [`Exhibits/FluidPath/Results/Verification.md`](../../../Exhibits/FluidPath/Results/Verification.md).
The null adapter validates commands/shader compilation only. Software-Vulkan and browser results are listed separately
in the verification report; neither is a hardware-GPU or Windows result.

## Recovered Fluid Editor: initial map and remaining work

- `Experimental/Fluid/src/FluidPanel.js` owns the editor, scene controls, backend selection and simulation loop;
  `SceneSpecification.js`, `SceneTomlCodec.js` and `SampleScenes.js` define the authoring contract and TOML export.
- `engine-webgl2.js` and `shaders-glsl.js` contain the tested WebGL2 simulation and rendering path. It advects,
  computes divergence, iterates pressure and projects velocity; obstacle and shadow controls are separate inputs.
- `engine-webgpu.js` and `shaders-wgsl.js` are the experimental WebGPU path; this browser run fell back to WebGL2,
  so the WebGPU path has **not** been validated by this test. Its feature set is explicitly reduced relative to WebGL2.
- The scene tests and the software-rendered browser checks exercise authoring and live GPU fields, not numeric parity
  against a reference solver. The recovered solver's discretization, boundaries, stability, collision behavior and
  shadow correctness still need independent measurement. Do not import conclusions from the other liquid prototype.
- Compare this editor's domain and authored settings against the Vehicle HMI's moving coarse/fine plume grids before
  selecting a shared spatial contract; define source, wind and force interchange with the Particle Editor. The browser
  apps currently run independently. Port the confirmed contracts to the actual target C++/Vulkan/Slang engine, and
  validate shadow integration and performance on a physical GPU. No such production integration is present yet.

## Earlier prototype audit (not the recovered Fluid Editor)

The liquid rows below describe only `Experimental/FluidEditor/`, which the user identified as the wrong target.
Do not transfer these numerical findings to the recovered `Experimental/Fluid` solver.

```text
Vehicle tyre/source -> PlumeSourceProjection -> moving coarse velocity lattice -> divergence / pressure
    -> projected velocity -> fine thermo advection -> light lattice -> depth-clipped volume march
Particle authoring -> wind lattice + acceleration fields -> particle update (separate implementation)
Fluid graph -> liquid particles -> scatter -> classify / forces -> pressure -> gather -> surface density -> raymarch
```

| Priority | Finding and source | Action |
| -------- | ------------------ | ------ |
| Critical | Liquid `Fluid/FluidKernels.js`: central divergence and central gradient compose a two-cell stencil, but Jacobi uses nearest neighbours. `ProjectionValidation.mjs` demonstrates the interior mismatch. More Jacobi iterations cannot make these operators equivalent. | Design a consistent discretization and boundary treatment; compare post-projection divergence to a reference before porting. Do not label the current result incompressible. |
| Critical | Liquid scatter had concurrent non-atomic writes to the same cell marker. | Changed scatter marker writes to `atomicStore`; WGSL compiles on Dawn null. GPU execution and stress tests remain required. |
| High | Liquid divergence visualization previously read the pre-projection field. | Recompute it after projection. Command-order test passes; numerical GPU validation remains required. |
| High | Liquid pressure ping-pong formerly restarted and visualized the primary buffer after odd iteration counts. | Fixed buffer tracking across substeps, inspection, reset and reallocation. Host routing regression passes; numerical shaders were tested separately on software Vulkan. |
| High | Liquid scatter accumulates fixed-point `i32`; concentrated particles / velocity can overflow. | Quantify worst-case occupancy and momentum; detect or prevent overflow, test dense/fast cases. No safety claim from current comments. |
| High | Particle Editor `js/engine.js`, `js/forcefields.js`, and `js/shaders.js` use their own 24×12×24 flow grid. Vehicle `Source/PlumeSpace.js` has its own moving lattice and ambient wind. | Define one directional field interface; distinguish velocity in m/s, acceleration in m/s², and damping in 1/s; preserve selection channels and order. No shared field exists yet. |
| High | Vehicle `Shader/PlumeMarch.wgsl` receives cascaded scene shadows and uses a plume light lattice for self-attenuation. Liquid `FluidRenderKernels.js` shades against sky/light without a scene shadow input. | Add liquid receive/cast shadows and volume transmittance into the scene lighting path; test opaque blockers, fluid self-shadow and moving lights. No full scene casting claim yet. |
| Medium | Liquid uses Y-up gravity; vehicle plume uses Z-up world coordinates (`PlumeSpace.js`). | Specify a single world-space transform, units, handedness and texture/voxel conventions; test vectors, colliders, and source locations on both sides. |
| Medium | Vehicle `SpatialGrid.js` wraps XY and recentres with a guard band; its thermo and velocity grids differ in resolution. | Preserve negative-coordinate floor and positive modulo, prior/current origins and clear newly exposed regions; never apply toroidal wrap to permanent deposited surfaces. |
| Medium | Vehicle step is clamped and has fade/idle gating; liquid substeps clamp elapsed time. | Verify stalls, pause/reset, teleport and fixed-step accounting; do not equate prototype timing with production performance. |

## Native work in this checkout

- `Engine/Fluid/SpatialGrid/CellWindow.{h,cpp}`: bounded CPU port of world-cell addressing, positive modulo, and
  two-dimensional recentering. It is not wired to a Vulkan image, solver, particle editor or renderer.
- `Exhibits/FluidPath/Code/CellWindowValidation.*`: deterministic comparison against the actual Vehicle HMI JS
  functions, including negative cells, forced moves, wraps and rejected invalid inputs.
- `Experimental/FluidEditor/Fluid/FluidViewport.js`: recomputes displayed divergence after projection and tracks
  the latest pressure buffer across substeps and inspection.
- `Experimental/FluidEditor/Fluid/FluidKernels.js`: atomic liquid cell classification during particle scatter.
- `Experimental/VehicleHmiPrototype/Source/`: reference import paths adjusted for the recovered layout.
- `Experimental/VolumetricFlow/index.html`: corrected unescaped comment delimiters that prevented the module parsing.
- Fluid/gas external font requests removed; Particle Editor reuses the locally recovered licensed DM Sans files.

## Implementation sequence

| Order | Proposed ownership | Deliverable and acceptance |
| ----- | ------------------ | -------------------------- |
| 1 | `Engine/Fluid/SpatialGrid/` | Extend the tested address contract to window changes, exposed-cell initialization, velocity/thermo transforms and shader parity. Include negative boundaries, teleport, wrap and no-source idle tests. |
| 2 | `Engine/Fluid/` and `Engine/Shaders/` | Port vehicle gas transport into C++/Vulkan and Slang with explicit resource lifetime, image layouts and compute barriers. Cover origin histories, pressure ping-pong, coarse/fine grids and device limits. |
| 3 | `Engine/Fluid/` and `UI/Editor/` | Define TOML-backed authoring interchange for fluid sources, wind/force fields and particle receivers; map coordinate spaces and units. Never interpret gravity as wind. Persist selections and reject unsupported types visibly. |
| 4 | `Engine/Fluid/` and `Engine/Shaders/` | Identify the requested liquid editor before choosing an algorithm; audit and verify its projection before porting. Keep liquid and gas distinct but share spatial and force contracts. |
| 5 | Renderer integration under the target engine's established ownership | Implement shadow receiving and casting for gas and liquid, transmittance, depth compositing, temporal reset and moving occluders. Define bounded quality budgets and debug views. |
| 6 | `Exhibits/FluidPath/` | Compare browser and native numeric fields, pressure residuals, conserved quantities, masks and image captures. Validate idle/active transitions, GPU errors, memory, p50/p95 time and Windows physical GPU. |

Slang/Vulkan and toml++ follow the referenced `CLAUDE.md` guidance; the required four `EngineDocs/AgenticInstuctions/`
skills were read at `eosclient0001-rgb/Frontier@1ae0bd6e9df6e4dcc553791bd8dd7cc620f6aebf`. They are not present in
this minimal checkout. The target production engine and renderer must be restored or integrated before selecting exact
Vulkan interfaces; these proposed paths are not a claim that the missing engine already owns them.
