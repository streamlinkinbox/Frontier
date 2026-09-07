# Frontier · Terrain Lab

A working, local-first terrain workbench for a **bounded, volumetric SDF**, built with TypeScript, React, and native WebGPU. The terrain is actual 3D geometry, not a heightmap, a photograph, or a textured plane.

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

## What you can do

- Start from a seeded sandstone canyon, an asymmetric weathered arch with unequal shoulders and alcoves, or fractured hoodoos/fins with caprock and non-monotonic profiles. The v0.2 canyon generator is unchanged; arches and spires use revised formations. Existing imported voxel data is preserved.
- Orbit, pan, or fly through a **96 × 48 × 96 meter** volume. This is not an infinite world. Right-mouse look + WASD/QE uses a free, Unreal-style inspection camera, independent of the rendering frame rate.
- **Add, carve, smooth, or flatten** using spherical 3D brushes. Carving supports tunnels, caves, and overhangs.
- Run and pause erosion, advance exactly one iteration, and change rainfall, erodibility, sediment capacity, evaporation, thermal relaxation, and layer resistance while it runs.
- With **Live preview** enabled, changing a simulation parameter queues 12 more iterations even when continuous simulation is paused. Parameters affect future simulation, rather than retexturing or regenerating the terrain.
- Inspect the material-free clay view or the runoff/erosion/deposition diagnostic view.
- Hold **C / Compare** to view the procedural original without destroying edits.
- Undo/redo up to four volume snapshots. Sculpt strokes, simulation starts, paused live-preview batches, and resets are snapshot boundaries.
- Adjust sunlight, exposure, water level, and subtle wind-driven water ripples. **Environment → Surface detail** controls world-space mineral grain, pore relief, broken lamination, and joint fractures. Analytic noise gradients drive the bump normals; finer layers fade with pixel footprint to reduce distant aliasing. **Renderer options → Native resolution** locks full viewport resolution for inspection.
- Save the complete project to this browser's IndexedDB, reopen that save from the project menu, or import/export portable `.frontier` files.
- Export an **indexed GLB mesh**, including smooth normals and procedural sandstone vertex colors. Extraction runs in a worker. PNG export captures the actual rendered viewport.

### Navigation

Click the viewport to focus keyboard input. The fly camera has no terrain
collision, like an editor inspection camera; it can enter caves and pass through
rock without depending on a heightmap. Input is cleared on focus loss, mouse
capture loss, and tab visibility changes so movement cannot get stuck.

| Input                                   | Action                                              |
| --------------------------------------- | --------------------------------------------------- |
| Right drag                              | Free look: rotate **in place**, not around the tile |
| W / S                                   | Fly forward / backward along the view direction     |
| A / D                                   | Strafe left / right                                 |
| Q / E                                   | Move down / up on the world's vertical axis         |
| Shift while flying                      | 3× speed boost                                      |
| Right mouse + wheel                     | Adjust fly speed (0.25–80 m/s)                      |
| Ordinary wheel                          | Dolly in fly mode / zoom in orbit mode              |
| Left drag in Navigate mode / Alt + drag | Orbit                                               |
| Middle drag / Shift + left drag         | Pan                                                 |
| Left drag with a brush                  | Sculpt the volume                                   |
| V / B / X / M                           | Navigate tool / add / carve / smooth                |
| [ / ]                                   | Smaller / larger brush                              |
| F / G                                   | Return to the framed overview / toggle grid         |
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
   **https://streamlinkinbox.github.io/Frontier/**. The footer should say **v0.2.3**.

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

A raymarcher renders the zero isosurface directly with trilinear volume samples, finite-difference normals, soft shadows, ambient occlusion, procedural bedding and mineral grain. There are no Quixel assets or texture downloads. The only raster assets in this repository are small **thumbnails captured from this renderer**. Inter and IBM Plex Mono are locally bundled, OFL-licensed fonts.

The water preview uses geometric shoreline clipping, depth-dependent absorption, Fresnel reflection rays against the SDF, and animated wind-driven normals. Exposure and camera-relative atmospheric attenuation finish the viewport. Render resolution adapts to measured frame rate, independently of voxel resolution. A single WebGPU frame is in flight; WebGL2 also uses fence-based backpressure. Both backends raymarch into an owned, adaptively sized off-screen texture and scale it into a stable display-sized canvas. Only actual viewport-size changes resize that canvas, immediately before drawing; Adaptive/Native quality changes and capture do not reset it. Adaptive resolution uses multi-second hysteresis; Native mode renders at full viewport resolution. GPU readback for PNG export happens in the same submission into an owned buffer, before swapchain recycling. Verification still checks the actual presented canvas, not just the off-screen image.

### Erosion pipeline

Each iteration performs:

1. **Face flux:** pressure differences and a downward gravity bias move water across the positive X/Y/Z faces of the 3D grid. Each face has one signed flux, shared by its two cells. A 1/6 donor limiter prevents negative water.
2. **Transport and forcing:** the divergence of those fluxes updates water; donor concentrations advect sediment. Rain is added near upward-facing, sky-exposed rock. Evaporation and the bounded edge drains remove water.
3. **Hydraulic exchange:** local wetness, flow, capacity, and stratigraphic hardness evolve the narrow-band level set and suspended sediment. Sediment above carrying capacity can deposit.
4. **Thermal relaxation:** a curvature-based term rounds exposed edges and relaxes the 3D surface. Resistant and soft strata weather at different rates.
5. **Distance maintenance:** every second iteration, a sign-limited Godunov/Eikonal relaxation pass extends distance changes into the surrounding volume. Brushes also periodically redistance. This keeps repeated editing from exhausting the original narrow band.

Both GPU and CPU paths have the same basic solver. The CPU path uses a single iteration per simulation tick to keep the interface usable. A session pauses at 8,000 iterations; reset restores the original terrain and can itself be undone.

### Important limits — not a claim of geological accuracy

This is a **creative, physically motivated erosion prototype**, not a validated geological prediction or a production engine integration.

- Flow uses a simplified 3D finite-volume pressure/gravity model. It is **not** incompressible Navier–Stokes, shallow-water CFD, SPH, or a calibrated real-world timescale.
- Internal water/sediment **advection** is conservative before forcing, saturation clamps, and edge drainage. Conversion between rock distance and sediment is approximate and is **not globally mass-conserving rock mechanics**.
- Thermal erosion is curvature relaxation, **not** rigid rock fracture, a talus-angle solver, or particle-based landslides. The wind slider animates water; it does not simulate aeolian rock erosion.
- Rain visibility uses coarse upward volume probes. Thin roofs, very small channels, and sub-voxel rock detail are limited by the grid.
- Eikonal relaxation approximately preserves the zero surface; it is not exact signed-distance reconstruction. Long or extreme edits can soften fine features.
- The water surface is a **separate planar visualization**, clipped and shaded against the actual terrain. It is not a reconstructed free surface of the mobile-water voxels. Use **Flow** to inspect simulation water.
- Geometry details below the voxel spacing are shaded procedural grain, not additional triangles. Increase the volume dimensions in `types.ts` for more geometric detail, with cubic memory/compute cost.
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
  display.ts             Safe GPU→CPU bitmap presentation and display-mode choice
  shaders/common.wgsl    Field sampling, noise, ray intersections
  shaders/compute.wgsl   Initialization, runoff, erosion, sculpting, redistancing
  shaders/render.wgsl    Procedural rock, lighting, diagnostic view, water
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
