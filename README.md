# Frontier · Terrain Lab

A working, local-first terrain workbench for a **bounded, volumetric SDF**, built with TypeScript, React, and native WebGPU. The terrain is actual 3D geometry, not a heightmap, a photograph, or a textured plane.

## Satellite terrain texturing · v0.8.0

Continues the linked `arena/01a07d13-frontier` application at `e15424a` (downstream flow, camera modes and layered rock relief). The existing WebGL workbench is retained; the new texturing path works in **both WebGL2 and WebGPU**.

Open **Materials → SatMaps**, or use `?renderer=webgl&panel=materials`.

- **Image-derived color, not the old procedural pigment shader.** Four local USGS/NASA satellite palettes: Namib dunes, Canyonlands, Iceland's volcanic coast and White Sands. This is a Gaea-style CLUT workflow; Gaea's proprietary assets/implementation are not included.
- **Terrain mask mixer:** normalized elevation, slope from actual normals, signed SDF curvature, geometric AO, terrain-derived D8 catchment, live runoff and net deposition. Source-image luminance supplies optional mip-filtered triplanar detail, including on cliffs/overhangs.
- **Eleven views:** lit surface, unlit albedo, composite texture mask, height, slope, curvature, occlusion, world-space normals, flow, deposition and photo detail. Water/lighting cannot obscure diagnostic maps.
- **Color controls:** clip low/high, bias, contrast, saturation and reverse. Choose Balanced / Exposed rock / Alluvial mask recipes without regenerating terrain.
- **Bring your own imagery:** local PNG/JPEG/WebP extraction, or horizontal/vertical color-strip import that preserves its order. Custom pixels and controls travel inside saved `.frontier` files. Export the source CLUT as a 256 × 1 PNG.
- **GLB export follows the satellite palette**, using the same data inputs to produce unlit linear vertex colors. This is a vertex-resolution approximation, not a UV texture bake. Micro-bump and water remain viewport-only.
- Old projects retain their appearance through **Legacy rock**. Changing a palette does not edit voxels, trigger erosion or replace sculpting history.

See **[satellite texturing: workflow, sources and limitations](public/research/satellite-texturing.md)**. The bundled imagery includes enhanced/false-color composites, not measured rock reflectance. Curvature/AO and image relief are visual approximations; catchment is top-surface D8 with closed depressions retained, not hydrodynamic simulation.

Source extraction can be reproduced with `npm run build:satmaps` (Node 22+, original images cached under `.cache/satmaps/`). Tests independently recreate all palettes/detail data from the committed local source crops.

## Run

```sh
npm ci
npm run dev
```

Open the URL printed by Vite. The development server binds to `0.0.0.0` and accepts Arena's `*.e2b.app` preview hosts. Everything runs in the browser; no backend, API key, external asset service, or remote texture request is required.

```sh
npm run build      # TypeScript check + production build
npm run build:pages # Production build with relative assets for project-based Pages
npm run publish:pages # Rebuild the prebuilt docs/ site committed for branch-based Pages
npm run preview    # Serve the production build
npm test           # Mathematical, simulation, archive, and mesh tests
npm run test:e2e   # Browser integration tests (install Chromium first)
npm run test:pages # Rebuild docs/ and test it under strict static /Frontier/ hosting
```

WebGPU requires a secure origin (HTTPS or localhost), a compatible browser/device, and enabled hardware acceleration. If unavailable, Frontier uses an explicitly labeled **WebGL2 / CPU fallback**, with a lower-resolution 3D volume. `?renderer=webgl` forces that path for testing. CPU/software graphics are substantially slower than a physical GPU; frame rate is measured, not simulated.

## If the controls load but the viewport is blank

Use the **Renderer options** (chip icon) in the viewport toolbar to restart the
renderer or select **Use WebGPU safe display** (keeps GPU erosion). The separate
**Use compatibility renderer** option uses WebGL2/CPU erosion. You can also open the preview
in its own browser tab. The compatibility path is still a sculptable 3D volume;
it is not a static screenshot or a heightmap.

Startup now verifies nonblank pixels copied from the actual render target before
reporting the scene as ready, and automatically falls back if WebGPU times out or
fails that check. A late, cancelled initializer cannot append an empty canvas on
top of its replacement. Every viewport has exactly one active canvas, including
across hot reloads.

Since **v0.2.1**, adaptive resolution uses an **off-screen render
target** in both backends. Switching Adaptive/Native no longer resets the visible
canvas bitmap. PNG exports copy owned render pixels, not a potentially recycled
WebGPU canvas. A GPU that stops responding for 15 visible, non-busy seconds, a
lost device, or a failed render/compute submission shows an explicit recovery
panel and stops the simulation. Background tabs and busy exports do not trigger
the stalled-frame deadline.

Restarting/reloading discards unsaved edits; it does not silently regenerate your
terrain. Save or export first when the GPU is available. After a lost GPU, reopen
a previously saved/exported project. The **F** shortcut restores the overview if
you have simply flown away from the terrain.

### Safe display and hidden-frame startup (v0.2.3)

A reported Chrome/Windows/NVIDIA case completed thousands of GPU frames and read
back opaque sandstone pixels, but the native canvas remained visually blank.
The report also showed that its first frame was acquired while the iframe was
hidden. This points to a display/presentation problem, not a shader compilation
error; the exact driver/compositor fault has not been proven on that device.

- **Windows defaults to Safe display.** WebGPU still initializes, raymarches,
  sculpts and erodes the full **144 × 72 × 144** volume. Each finished GPU frame is
  copied to an owned buffer, unpacked to RGBA, and painted into a regular,
  software-backed Canvas2D bitmap. This mode never acquires a native WebGPU canvas
  or copies its swapchain through `drawImage`/`toBlob`.
- Safe display costs a GPU→CPU frame transfer and bitmap copy. It is explicitly
  labeled **COMPUTE · SAFE DISPLAY**, not native presentation or CPU erosion.
  Single-frame backpressure bounds the transfers and buffers are reused.
- **Renderer options → Use WebGPU (native display)** selects the faster native
  path. `?display=safe` / `?display=native` explicitly select either mode. The
  defaults are safe on Windows, native elsewhere; these are workbench recovery
  choices, not a claim that every Windows GPU is defective.
- Native `configure()` / the first draw now wait for a **visible animation
  frame**. Returning from a hidden tab/iframe renews the native surface before
  drawing, without regenerating or discarding the SDF. Cancellation prevents an
  obsolete hidden initializer from reviving later.
- The fractional DPR in the report (0.5) is supported; it is not treated as a
  rendering error. A browser regression uses that DPR with a Windows user-agent
  and deliberately disables native WebGPU canvas creation while verifying actual
  visible sandstone, GPU erosion, and exact undo. This emulates the configuration
  using the test machine's GPU, not the user's physical NVIDIA GPU.

Switching display modes through the menu reloads the app; save/export first if
there are unsaved edits. A mode switch is not a silent terrain reset.

### Share GPU diagnostics (v0.2.2+)

The **GPU logs** button in the header is available even if initialization fails.
The error panel also has **Show GPU logs**. You do not need DevTools.

1. Click **GPU logs → Check viewport**. The engine briefly holds rendering and
   erosion while inspecting the current frame; it does **not** edit, reset or
   save the volume.
2. Click **Copy GPU logs** and paste the report into the chat. If clipboard access
   is blocked by the preview iframe, the text stays selected for **Ctrl+C / Cmd+C**.
   **Download logs** provides the same report as `frontier-gpu-diagnostics.txt`.
3. Include whether the main viewport is still blank. `?diagnostics=1` opens the
   panel directly, in either the normal or compatibility renderer.

The bounded in-memory log includes browser/iframe/secure-context details, GPU
adapter/features/limits, shader compilation and device errors, fallback reasons,
canvas dimensions and CSS ancestors, queue progress, and initial-frame pixel
statistics. The manual check also samples the presentation texture, an owned
off-screen render, and SDF statistics (solid count, distance
range, non-finite samples). Copy/download snapshots current state even when the
GPU is unavailable. Live report updates can be paused for selection.

**GPU readback and FPS do not prove on-screen presentation.** The report labels
GPU target checks separately rather than calling them a screen capture. It never
copies the live WebGPU canvas through Canvas2D or `toBlob()`: some drivers block
synchronously while reading a recycled swapchain image, which cannot be bounded
by a JavaScript timeout. A CSS-hidden canvas is covered by a regression test
precisely because it can still report successful GPU work.

Nothing is uploaded automatically or persisted by the logger. URL query strings,
hashes, credentials, credential-like fields and raw typed-array/voxel buffers are
omitted from the report. The report does contain browser/GPU identification and
app-origin information; it is shared only when you copy/download it.

## Water, weathering and material update · v0.3.0

The **working WebGL2 path** and WebGPU use the same new rendering shader and
corresponding CPU/GPU erosion equations. This update does not claim that the
reported Windows native-WebGPU presentation problem has been validated as fixed.
Continue using `?renderer=webgl` if that is the reliable renderer on your device.

- **Water:** four non-parallel, dispersion-driven wave bands displace the surface,
  rather than only wobbling the normals of a flat sheet. Terrain-distance ripples
  lap toward the banks, with broken contact foam, a moving wet edge, restrained
  highlights, refracted riverbed detail, and shallow caustic light. Optical clarity
  and actual suspended-sediment samples affect the water tint.
- **Stone:** rotated multi-scale grain removes the old axis-aligned pore pattern.
  Broken lamination, quartz variation, anti-aliased joints, vertical varnish,
  mineral tide marks, dark wet rock and a wetter specular response add scale.
  Erosion history reveals fresher surfaces; deposition adds a sandy finish.
  The same dipping/warped strata determine both the material and its resistance.
- **Physics:** tangential runoff must overcome a cohesion-dependent shear threshold
  to detach rock. Deposition spends the cell's own suspended material (no borrowed
  neighbor sediment), with an explicit local solid-fraction/sediment budget.
  Sediment has a bounded downward settling flux. Talus travels down four diagonal
  links only above its repose angle, with paired donor/receiver budgets, rather
  than a global Laplacian blur. Every voxel above a surface is checked for rain
  shelter, so a one-voxel roof protects a cave floor.
- **Controls:** Hydraulic → **Scour & sediment controls** contains **Rock cohesion**
  and **Settling rate**. Thermal has **Talus weathering**, **Layer resistance** and
  **Talus angle**. **Water clarity** is available in both water panels. Process
  controls use the existing 12-iteration live preview; water clarity is visual only.

Existing `.frontier` archives keep their saved voxel geometry. Missing new settings
receive defaults, and future iterations use the revised solver. Mesh exports share
the new base strata/sand colors, but do not bake the optical water or shader-only grain.

## Sculpting & incision update · v0.4.0

### Eight volume brushes

| Brush | Shortcut | What it actually does |
| --- | --- | --- |
| Add | B | Builds the SDF outward inside a soft spherical brush. |
| Carve | X | Removes a spherical volume. |
| Smooth | M | Relaxes the local distance field. |
| **Flatten** | **L** | Samples one plane at mouse-down and holds it for the entire stroke. Choose the averaged **Surface plane** or **Horizontal**. A cylindrical footprint converges to that plane without repeatedly lifting its target as the terrain moves. |
| **Ridges** | **R** | Preserves the old Flatten behavior: a moving horizontal shelf with spherical falloff. |
| **Cracks** | **K** | Subtracts an uneven, branching, tapered fault in the surface frame. Drag to connect fractures. |
| **Crevice** | **U** | Subtracts a wider, deeper V-shaped fissure, following the drawn stroke. |
| **Boulder** | **O** | Unions an irregular, partly embedded ellipsoidal rock. Drag to space more rocks; holding still does not stack them repeatedly. |

Depth/height and opening-width controls appear for feature brushes. All of these
change the **3D signed-distance volume** and therefore affect caves, silhouettes,
collision/mesh extraction and GLB export; they are not decals. One stroke is one
undo boundary. The plane is resampled only on the next stroke. Feature widths
have a voxel-aware minimum: the 1 m WebGL grid cannot represent a hairline crack.

### Rain channels, wind flutes, and separate talus

The previous direct rain-impact term was too uniform, and talus could dominate
the resulting shape. Rain no longer directly subtracts distance from every
exposed cell. Instead, water moves using gravity projected onto the local **3D
surface normal**, pressure differences, and a small seeded roughness potential.
The potential routes water; it does not directly stamp grooves. Across-flow
water concentration gates/amplifies detachment so flowing channels cut deeper
than background sheet wash. The evolving geometry then affects later routing.

A separate **Wind** tab now controls **Wind abrasion** and **Wind direction**.
Dry, wind-facing surfaces are sampled for upwind shelter before abrasive grains
remove SDF rock. A seeded, direction-aligned grain-flux spectrum gives uneven
bands/flutes; detached dry sediment has a donor-budgeted downwind flux and can
settle. This is a phenomenological abrasion model, not resolved saltation or CFD.
The existing **Wind strength** control in Water still affects the visual waves.

To inspect each process without masking it:

1. Choose **Rain cuts** or **Wind streaks** above the erosion tabs. These change
   settings and pause the current run; they **do not regenerate your terrain**.
2. Click **Weather 80 iterations**, or run continuously. Pause interrupts the batch.
3. Use **Clay** or **Flow** to inspect geometry and transport without the material.

The new default talus strength is **0.03**, rather than 0.3. Old saved projects
keep their chosen strength; use a process preset to disable talus when comparing
incision. Wind abrasion starts off, and does not affect waterlogged/underwater
rock. A wet project may need time to dry before wind cuts become apparent.

There is still no rigid-block collapse/fracture solver. Aggressive weathering can
leave unsupported fragments and grid-scale edges, and the time/strength units
are artistic rather than geological. This update makes distinct volumetric cuts;
it does **not** claim calibrated erosion or sub-voxel fracture mechanics.

Engine integration changes: `Backend.pick` returns `{ position, normal }`.
`BrushStamp` contains the locked origin, normal, tangent, previous point and seed.
`TOOL_IDS` centralizes brush codes (legacy Ridges is code 4; new Flatten is code 5).
The shared uniform block is now **20 vec4s / 320 bytes**; use `UNIFORM_BYTES`, and
update copied GLSL/WGSL layouts together. Archives keep their existing voxel data
and receive defaults for missing v0.4 controls.

## Surface materials & water optics · v0.5.0

Open the new **Materials** inspector tab (or click the material in the Scene tree).
The selectable models are **Sandstone, Limestone, Granite and Basalt**. This pass
changes surface rendering and optics, **not brushes, sculpted geometry, or erosion
parameters**. Preset selection and Reset material only affect material settings.

The research and implementation rationale are in
**[public/research/materials-and-water.md](public/research/materials-and-water.md)**, with sources
from Google Filament, PBRT, Epic's water documentation and geological references.
Colors/roughness are explicitly **authored starting values**, not measured scans.

- Separate linear-RGB albedo, dielectric IOR/Fresnel reflectance, correlated-Smith
  GGX specular response and diffuse energy attenuation replace the old fixed
  sandstone lighting/specular-power treatment.
- Grain size and relief have **millimeter units**. Sub-pixel grains are averaged
  into the material response rather than appearing as enlarged white specks.
- Sandstone uses granular cement/bedding, limestone a fine mottled matrix, granite
  cellular mineral domains with distinct mica/quartz/feldspar-like responses,
  and basalt a fine dark matrix with controllable vesicle-style pores.
- Controls: base color, roughness, grain size, relief, mottle/pore scale, pore
  coverage, sedimentary layer contrast, weathering, effective IOR and moisture.
  Rock remains non-metallic. Surface detail is a final normal-detail gain.
- Material values save/load with the project. Older projects acquire defaults
  without modifying their saved volume. GLB exports carry broad-band color,
  selected roughness and `KHR_materials_ior`; millimeter shader detail and water
  are not falsely baked into coarse vertex colors.

**Water edge repair:** clearance-limited offsets and entry-only refraction traces
avoid starting inside a bank and treating its far exit as deep water. Unknown
traces no longer invent an 8 m absorption layer. Shoreline coverage is smooth,
volume/foam contributions are lit, and above/below-water IORs and total internal
reflection are handled separately. Underwater optical paths are attenuated before
edge coverage is mixed, preventing a tiny water sliver from shortening the entire
pixel's path. **Absorption distance** and **Shore foam** controls are also available.

Water can still physically reflect blue sky or tint a deep view blue/green; the
fix targets an unjustified colored border. This is RGB real-time optics with an
analytic sky/lighting approximation—not a spectral, path-traced or calibrated
multilayer wet-rock model. Relief is shaded; the voxel grid does not resolve grains.

Uniform integration note: the material/optical parameters extend the common block
to **24 vec4s / 384 bytes**. Use `UNIFORM_BYTES`; keep WGSL and GLSL layouts in sync.

## Slate-inspired workspace & non-repeating river · v0.6.0

The UI follows the supplied [Slate base component reference](https://github.com/SultanAladin/Slate/blob/arena/01a062a4-slate/References/UIComponents.html)
(reference commit `37d8614`): near-black panels, rounded groups, white primary actions,
quiet strokes and a violet keyboard-focus accent. Frontier retains its local Inter /
Plex fonts rather than adding an external font service. The old layered stylesheet
was replaced with one token-driven workbench theme.

- Bordered scene/inspector panels, a compact project bar and a cleaner viewport
  heading replace the previous overlaid title and mixed green/gold treatments.
- Sliders have editable value pills. Click a value, type, then **Enter** to commit;
  **Escape** cancels. Values clamp to their valid ranges; ordinary ranges remain
  keyboard accessible. Percent controls accept percentages; physical controls
  accept their displayed units. Opening widths respect the voxel-aware minimum
  (the input tooltip gives the editable range).
- Menus render in a positioned portal rather than being clipped by rounded panels.
  Arrow keys navigate menu items; Escape closes and restores trigger focus.
- Narrow layouts use a dismissible sculpting drawer and a stacked inspector. No
  preview-breaking compositing filter is applied to the actual terrain canvas.

**River surface:** the former four periodic sine bands are replaced by a
world-space, domain-warped, advected noise field. Three differently oriented
scales travel at slightly different speeds, without wrapped texture UVs or a
looping phase reset. The same analytic height derivatives drive normals and the
water intersection; filtered glints reduce regular-looking bright flecks.

Water controls now include **Current speed**, **Flow direction** and **Ripple
scale** under **Current & ripple shape**. Direction is travel direction: 0° = +X,
90° = +Z. Zero wind and zero current produce a still surface. These controls affect
water visualization, **not the erosion solver or a physically solved river velocity
field**. Optical depth/refraction guards and the four material models are retained.

`river.ts` is a CPU reference for the shader field. Tests check analytic derivatives,
height/slope bounds, directional motion and non-repetition across likely short tile
shifts. Browser tests cover actual moving water pixels, numeric editing, unclipped
keyboard-operated menus and the mobile drawer. A spatial correlation check does not
claim that a procedural river is a calibrated hydrodynamics model.

Uniform layout: **25 vec4s / 400 bytes** (`UNIFORM_BYTES`). Old projects receive
river-control defaults without changing their stored terrain.

## Downstream flow, explicit cameras & layered relief · v0.7.0

### River that visibly travels downstream

Under **Environment → River surface → Downstream current**, select **Follow canyon**
or **Compass heading**, then set speed and optionally **Reverse current**. For the
canyon, forward flow travels from +Z toward −Z along the seeded course; reverse
travels the opposite way. A 32-sample arc-length table with Hermite interpolation
keeps speed meaningful around bends. Foam/current streaks are advected in that
coordinate system, rather than merely pulsing in place. **Current streaks** controls
their visibility. The default current for a new project is now 0.8 m/s.

The channel route follows the **seeded canyon**, not a new global hydraulic solve.
Water still clips against sculpted terrain, but heavily reshaped/custom courses can
use Compass heading. Other formation presets use heading flow. These are routed
surface currents and visual foam, not Navier–Stokes or transported water voxels.

### Orbit and Fly no longer fight each other

- Select **Orbit camera (1)** or **Fly camera (2)** explicitly in Camera controls.
- **Orbit:** left/right drag rotates, middle or Shift-drag pans, wheel zooms.
  WASD/QE do not activate Fly behind your back.
- **Fly:** right-drag looks in place; focused WASD/QE moves; wheel changes speed
  without dollying. A left drag does not switch to Orbit (it sculpts when a brush
  is selected). Middle/Shift-drag pans in the selected mode.
- F / Frame terrain and top view change the pose, **not the control mode**.
  Switching camera mode preserves the eye position and clears held inputs.
- “Live viewport” is render/simulation status, not a camera-mode name. The camera
  selector and the mode-specific hint line show the current navigation controls.

### Texture structure, not another material tint

**Materials → Legacy rock → Layered rock detail** adds a centimeter-scale height/normal stack
above the existing millimeter grain layer: domain-warped multi-octave noise, a
smoothed ridged component, broken sedimentary lamination and restrained cavity
shading. Sandstone and limestone receive layers; igneous presets retain unlayered
weathered relief. The noise is fixed in world space and filtered by pixel footprint.

Use **Subtle**, **Layered** or **Weathered** detail presets, or adjust **Rock relief**,
**Noise scale**, **Layer spacing**, **Layer relief**, and the octave/ridge/warp
controls. This changes surface normal/height detail without changing base color.
It is **shader relief**, not additional triangles or a modification of the saved
SDF. Clay view deliberately shows the underlying voxel geometry without it.

Tests cover explicit mode isolation and focus handling, constant-distance downstream
marker travel/reversal, arc-coordinate and relief derivatives, CPU/shader agreement,
and a rendered texture comparison on an unchanged volume. The common uniform block
is now **36 vec4 slots / 576 bytes** (including the eight-vector route table).

## What you can do

- Start from a seeded sandstone canyon, an asymmetric weathered arch with unequal shoulders and alcoves, or fractured hoodoos/fins with caprock and non-monotonic profiles. The v0.2 canyon generator is unchanged; arches and spires use revised formations. Existing imported voxel data is preserved.
- Orbit, pan, or fly through a **96 × 48 × 96 meter** volume. This is not an infinite world. Explicit Fly mode uses RMB look + WASD/QE, independent of rendering frame rate. Orbit is a separate, explicitly selected mode.
- **Add, carve, smooth, flatten, form ridges, crack, cut crevices, or stamp boulders** with 3D volume brushes. Carving supports tunnels, caves, and overhangs.
- Run and pause erosion, advance exactly one iteration, and change rainfall, erodibility, sediment capacity, evaporation, thermal relaxation, and layer resistance while it runs.
- With **Live preview** enabled, changing a simulation parameter queues 12 more iterations even when continuous simulation is paused. Parameters affect future simulation, rather than retexturing or regenerating the terrain.
- Inspect the material-free clay view or the runoff/erosion/deposition diagnostic view.
- Hold **C / Compare** to view the procedural original without destroying edits.
- Undo/redo up to four volume snapshots. Sculpt strokes, simulation starts, paused live-preview batches, and resets are snapshot boundaries.
- Adjust sunlight, exposure, water level, and subtle wind-driven water ripples. **Materials → Legacy rock → Surface detail** controls world-space mineral grain, pore relief, broken lamination, and joint fractures. Analytic noise gradients drive the bump normals; finer layers fade with pixel footprint to reduce distant aliasing. **Renderer options → Native resolution** locks full viewport resolution for inspection.
- Save the complete project to this browser's IndexedDB, reopen that save from the project menu, or import/export portable `.frontier` files.
- Export an **indexed GLB mesh**, including smooth normals and procedural sandstone vertex colors. Extraction runs in a worker. PNG export captures the actual rendered viewport.

### Navigation

Click the viewport to focus keyboard input. The fly camera has no terrain
collision, like an editor inspection camera; it can enter caves and pass through
rock without depending on a heightmap. Input is cleared on focus loss, mouse
capture loss, and tab visibility changes so movement cannot get stuck.

| Input                                   | Action                                              |
| --------------------------------------- | --------------------------------------------------- |
| 1 / 2                                   | Select Orbit / Fly camera controls                  |
| Right drag                              | Orbit in Orbit mode / look **in place** in Fly mode |
| W / S                                   | Fly forward / backward along the view direction     |
| A / D                                   | Strafe left / right                                 |
| Q / E                                   | Move down / up on the world's vertical axis         |
| Shift while flying                      | 3× speed boost                                      |
| Wheel in Fly mode                       | Adjust fly speed (0.25–80 m/s); never dolly          |
| Wheel in Orbit mode                     | Zoom the orbit camera                               |
| Left drag in Navigate mode / Alt + drag | Orbit                                               |
| Middle drag / Shift + left drag         | Pan                                                 |
| Left drag with a brush                  | Sculpt the volume                                   |
| V / B / X / M                           | Navigate tool / add / carve / smooth                |
| L / R / K / U / O                       | Plane flatten / ridges / cracks / crevice / boulder |
| [ / ]                                   | Smaller / larger brush                              |
| F / G                                   | Frame without changing camera mode / toggle grid   |
| Space                                   | Run / pause erosion                                 |
| Hold C                                  | Compare with original                               |
| Ctrl or Cmd + Z                         | Undo                                                |
| Ctrl or Cmd + Shift + Z                 | Redo                                                |

The view menu includes **Fly camera** and a fly-speed slider. Diagonal motion is
normalized and movement uses elapsed time, not rendered-frame count. Editor
inputs don't move the camera while typing into fields or using a dialog.

## GitHub Pages

A prebuilt, self-contained site is committed in **`docs/`**. It runs entirely in
the browser; **no development server or custom GitHub workflow is required**.

In **[Settings → Pages](https://github.com/streamlinkinbox/Frontier/settings/pages)**:

1. Select **Deploy from a branch**.
2. Choose **`arena/01a07d13-frontier`** and **`/docs`** (not `/`), then **Save**.
3. Wait for GitHub's Pages deployment to finish, then open
   **https://streamlinkinbox.github.io/Frontier/**. The footer should say **v0.7.0**.

### Why the earlier deployment returned 404

Pages was configured to serve `/` from `arena/01a07c42-frontier`. That folder held
the **source** `index.html`, which requested `/src/main.tsx` and an unresolved
`%BASE_URL%` favicon. GitHub Pages serves static files; it does not run Vite or
compile TypeScript. A successful Pages deployment can therefore still serve a
non-working source page. Select the **current branch + `/docs`**, not the old
branch or the source root.

As a safety net, the new source index recognizes when it has not been processed
by Vite and redirects to `./docs/`, preserving renderer query options and the
hash. All compiled assets now use **relative URLs**, so this rescue path also
works under `/Frontier/docs/`. Correct `/docs` publishing still serves the app at
`/Frontier/` with no redirect. If an application bundle is missing, an inline
loading/error screen explains how to recover instead of leaving the whole page
empty. **Do not copy the source index over the compiled `docs/index.html`.**

To refresh the deployment after source edits:

```sh
npm ci
npm run publish:pages
npm run check:pages
# Commit the updated source and generated docs/ output to this session branch.
# Push only arena/01a07d13-frontier.
```

`docs/` is generated by `scripts/publish-pages.mjs`; do not hand-edit it. The
script checks the compiled entry point, relative asset references, fonts,
presets, export worker, and `.nojekyll` **before** replacing the previous build.
This small generated deployment is intentionally tracked; dependencies, other
build output, test artifacts and large exports remain ignored.

`npm run test:pages` tests the **compiled** app with strict static hosting at
`/Frontier/`, both with `/docs` as the source and with the root-source rescue.
Missing files return real 404s (there is no Vite SPA fallback to hide bad paths).
The tests exercise both renderers, actual visible sandstone pixels, PNG capture,
the mesh-export worker, and missing-bundle recovery. No development-only globals
are available in this production build.

**Optional automation:** `deployment/pages-workflow.yml` is an inert template.
An authorized owner may copy it to `.github/workflows/pages.yml` and select
**GitHub Actions** as the Pages source. This requires workflow permission; it is
not necessary for branch-based `/docs` hosting. The template does not run while
stored under `deployment/`.

Local IndexedDB saves are **per browser origin**. Export a `.frontier` project
before moving from the Arena preview to Pages, then import it on the Pages site.
HTTPS hosting does not change browser/GPU support. The direct compatibility link
is **https://streamlinkinbox.github.io/Frontier/?renderer=webgl**.

## How it works

### Volume and rendering

The WebGPU volume is **144 × 72 × 144 voxels** (1,492,992 samples; approximately 0.667 m spacing). The fallback is 96 × 48 × 96 (1 m spacing). Two `rgba16float` 3D textures are ping-ponged on the GPU:

| Channel | Meaning                                                            |
| ------- | ------------------------------------------------------------------ |
| R       | Signed distance / evolving level set; negative is solid rock       |
| G       | Mobile water amount                                                |
| B       | Suspended sediment amount                                          |
| A       | Accumulated local erosion/deposition displacement, for diagnostics |

The initial field combines 3D CSG, warped polygonal mesas, ellipsoidal cutouts, irregular stratification, and volumetric noise. The arch preset explicitly has **multiple solid/empty crossings in a vertical column**. The CPU and WGSL initializers implement the same seeded functions.

A raymarcher renders the zero isosurface directly with trilinear volume samples, finite-difference normals, soft shadows and ambient occlusion. The default surface samples satellite-derived CLUTs using terrain masks; the explicit Legacy rock path retains its procedural dielectric materials. Image/grain detail is filtered by world-space pixel footprint. Small public-domain satellite crops, extracted palette/detail pixels, and renderer-captured formation thumbnails are bundled locally; no Quixel or proprietary Gaea assets or runtime texture downloads are needed. Inter and IBM Plex Mono are locally bundled, OFL-licensed fonts.

The water preview intersects a multi-band displaced surface and clips it against the SDF. It uses terrain-distance shoreline ripples and foam, refracted bed rays, depth-dependent absorption/turbidity, Fresnel reflection rays, and animated wind normals. Exposure and camera-relative atmospheric attenuation finish the viewport. Render resolution adapts to measured frame rate, independently of voxel resolution. A single WebGPU frame is in flight; WebGL2 also uses fence-based backpressure. Both backends raymarch into an owned, adaptively sized off-screen texture and scale it into a stable display-sized canvas. Only actual viewport-size changes resize that canvas, immediately before drawing; Adaptive/Native quality changes and capture do not reset it. Adaptive resolution uses multi-second hysteresis; Native mode renders at full viewport resolution. GPU readback for PNG export happens in the same submission into an owned buffer, before swapchain recycling. Verification still checks the actual presented canvas, not just the off-screen image.

### Erosion pipeline

Each iteration performs:

1. **Face flux:** projected surface gravity, pressure differences and a seeded
   roughness potential route water across shared X/Y/Z faces. It is a 3D surface
   calculation, not a heightmap. A 1/6 donor limiter bounds water exports.
2. **Transport and forcing:** flux divergence updates water, upwind concentrations
   advect sediment, and a reserved donor budget lets sediment settle downward.
   Rain enters exposed upward-facing surface cells; every voxel in the column is
   tested for shelter. Evaporation, water saturation and the outer drains are sinks.
3. **Hydraulic exchange:** across-flow concentration focuses tangential runoff
   incision above a cohesion/strata-dependent shear threshold. Rain is a water
   source, not a uniform geometry-subtraction term. Unloaded runoff can detach material;
   overloaded water can deposit on supporting faces. The local exchange uses
   `solidFraction = clamp(0.5 - sdf / (2 * cell), 0, 1)`, with equal and opposite
   changes to this material proxy and suspended sediment. Deposition cannot spend
   a neighbor's sediment, and still water without rain does not scour.
4. **Wind abrasion:** dry wind-facing rock is checked for upwind shelter, then
   directionally modulated grain flux cuts the SDF. Detached sediment is budgeted
   locally and can advect downwind or settle. Water, advection and dust have
   separate portions of the same donor budget.
5. **Talus transport:** a four-channel flux pass transfers material to lower diagonal
   neighbors, followed by an accumulation pass. Slopes below the selected repose
   angle do not move. Cohesion and lithology slow intact rock; deposited material
   is more mobile. Each link receives a quarter of donor/receiver budgets so the
   isolated pass conserves its solid-volume proxy and lowers material elevation.
6. **Distance maintenance:** every second iteration, sign-limited Godunov/Eikonal
   relaxation extends the new distances into the volume. This pass is not a
   volume-conservative reconstruction; overall geological mass conservation is
   therefore **not** claimed. Brushes also periodically redistance.

Both GPU and CPU paths have the same basic solver. The CPU path uses a single iteration per simulation tick to keep the interface usable. A session pauses at 8,000 iterations; reset restores the original terrain and can itself be undone.

### Important limits — not a claim of geological accuracy

This is a **creative, physically motivated erosion prototype**, not a validated geological prediction or a production engine integration.

- Flow uses a simplified 3D finite-volume pressure/gravity model. It is **not** incompressible Navier–Stokes, shallow-water CFD, SPH, or a calibrated real-world timescale.
- Internal advection/settling, the local hydraulic exchange, and the isolated talus pass have explicit material budgets. However, SDF-to-solid-fraction conversion is a proxy, floating-point storage rounds values, and redistancing changes the proxy. The whole solver is **not globally mass-conserving rock mechanics**.
- Talus is a slope-limited grid transfer, **not** rigid-block fracture or resolved granular dynamics. Cohesion and wind grain-flux variation are phenomenological. The Water panel's wind control animates waves; the separate Wind abrasion control changes geometry.
- Rain visibility checks the full vertical voxel column, but roofs or channels thinner than the grid cannot be represented accurately.
- Eikonal relaxation approximately preserves the zero surface; it is not exact signed-distance reconstruction. Long or extreme edits can soften fine features.
- The displaced water surface, caustics and contact foam are **visual approximations**, clipped and shaded against actual terrain. They are not a reconstructed mobile-water free surface, breaking-wave CFD, or shoreline wave-abrasion simulation. Use **Flow** to inspect simulation water. The preview's water level is not a hydraulic boundary condition.
- Geometry details below the voxel spacing are optional image-derived bump (SatMaps) or procedural grain (Legacy rock), not additional triangles. Increase the volume dimensions in `types.ts` for more geometric detail, with cubic memory/compute cost.
- GLB exports the terrain's current zero isosurface and sampled base vertex color. Water, sunlight, atmosphere, and shader-only grain are **not baked** into the mesh. Port the WGSL material/water shader to your engine for the full rendering treatment.
- Projects stay on this origin/browser until exported. There is no cloud sync or automatic save. A different device's resolution is handled with trilinear 3D resampling on import.

## Integration map

```text
src/engine/
  TerrainEngine.ts       Interactions, history, frame scheduling, deferred resizing
  EditorCamera.ts        Frame-independent orbit/fly camera and scoped movement
  WebGPUBackend.ts       Native device, 3D textures, compute/render pipelines
  WebGLBackend.ts        Explicit lower-resolution fallback
  presentation.ts        Owned RGBA/BGRA readback and PNG encoding
  materials.ts           Material presets, linear color and broad-band export color
  surfaceDetail.ts       CPU reference: layered cm-scale height/normal noise
  flowRoute.ts           Seeded course and arc-distance coordinates
  noise.ts               Shared analytic derivative-noise reference
  optics.ts              Testable dielectric/attenuation reference equations
  display.ts             Safe GPU→CPU bitmap presentation and display-mode choice
  shaders/common.wgsl    Field sampling, noise, ray intersections
  shaders/compute.wgsl   Initialization, runoff, erosion, sculpting, redistancing
  satmaps/              Satellite extraction, library, D8 maps, GPU textures and export sampling
  shaders/satmap.wgsl    Image CLUT sampling and terrain-mask mixing (also emitted as GLSL)
  shaders/terrain-height.wgsl  Compact GPU upper-envelope extraction for drainage
  shaders/render.wgsl    Satellite/legacy shading, lighting, diagnostic views, water
  shaders.ts             WGSL assembly + typed-subset GLSL emission
  field.ts               Matching CPU SDF, trilinear sampling, CPU brushes
  simulation.ts          CPU reference flux, evolution and redistancing
  mesh.ts                Edge-welded marching tetrahedra + indexed GLB
  export.worker.ts       Off-main-thread mesh extraction
  project.ts             Validated binary archive and IndexedDB storage
```

The `.frontier` binary format is versioned: eight ASCII bytes `FRONTIER`, a little-endian `uint32` padded JSON-header length, a UTF-8 JSON header padded with spaces to four bytes, then tightly packed little-endian float16 RGBA voxels. **X is fastest, then Y, then Z.** World bounds are `[-48, -10, -48]` to `[48, 38, 48]`; samples lie at voxel centers. The zero isosurface defines the rock. Imports validate the version, dimensions, settings, byte count, and finite sample values before allocation/use.

## Verification

The unit suite covers seeded determinism, real 3D overhang topology, boundedness, spherical edits, internal water/sediment conservation, no-forcing stability, finite erosion updates, sign-preserving redistancing, half-float archives, malformed imports, indexed mesh extraction and GLB headers.

Browser tests exercise actual volume changes, exact undo/redo restoration, live parameters, one-step semantics, save/export, presets, and mobile layout. Regression cases cover a late renderer-startup failure during remount, a GPU that fails its first-frame check, and the in-viewport compatibility action. Added regressions assert actual WebGPU
usage (a fallback is not counted as a GPU pass), stable visible-canvas dimensions
through quality changes/capture, sandstone pixels in PNGs, stalled-device recovery,
real device destruction, and caught compute errors. PNG unit tests cover RGBA,
BGRA, bottom-up WebGL rows, and encoding failures. To install a normal test browser:

```sh
npx playwright install --with-deps chromium
npm run test:e2e
```

Use `FRONTIER_TEST_PORT=5175` to keep browser tests separate from a running
production preview. For a software Vulkan environment, use `FRONTIER_SOFTWARE_GPU=1`. This selects
SwiftShader for both ANGLE and Vulkan; forcing ANGLE's generic Vulkan mode can
fail shared-image presentation on a headless build even when an adapter exists. An externally installed Chromium can be selected with `PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH`. Performance in a software renderer does not represent a hardware GPU.

The original uploaded `.patch` in this checkout is unrelated to this terrain workbench and has been left untouched.

### v0.3 numerical and visual regressions

The added tests cover still-water versus moving-water scouring, cohesion response,
local erosion/deposition budgets, no neighbor-borrowed deposits, one-voxel rain
shelter, sediment settling/advection conservation, stable below-repose slopes,
downhill talus and its material budget, legacy archive defaults, and CPU/GPU
agreement on an identical small 3D fixture (with a tolerance for RGBA16F storage).
Browser tests also check animated WebGL water pixels, optical clarity, the new
controls, actual GPU SDF changes, sculpting, exact undo and exports. These tests
validate implementation properties, **not geological calibration**.

### v0.4 geometry tests

Regression tests check horizontal/vertical flattening, convergence without plane
overshoot, legacy Ridges, crack/crevice empty voxels and depth, bounded boulders,
CPU/GPU brush agreement, real pointer-stroke plane locking, exact undo and no
stationary boulder stacking. Erosion tests measure **zero-isosurface crossings**
on uniform fixtures—not just changed distance values or colored pixels. They
check nonuniform rain incision versus sheet wash, directional wind bands, initial
shelter protection, and conserved dry-dust transport. Clay-shaded WebGL captures
also exercise the canyon after separate rain/wind runs. These are implementation
regressions, not field validation of a geological model.

### v0.5 optical/material regressions

Tests check linear color conversion, dielectric F0, BRDF reciprocity and rough
white-furnace bounds, real grain filtering, thin/zero-thickness transmission, safe
ray offsets, total internal reflection and coverage continuity. Browser checks
switch four materials on an unchanged volume, adjust their parameters, test an
actual GLSL entry ray only 2 cm from rock, and capture raised-water overhangs from
above and below. GLB material metadata and legacy defaults are also validated.
