# Fluid path verification

## Scope correction

The earlier liquid measurements and `Images/fluid.png` concern `Experimental/FluidEditor/`, the wrong liquid node
prototype, not the recovered `Experimental/Fluid` editor. They are retained as historical evidence only and do not
establish correctness of the requested editor. The intended separate liquid editor remains unconfirmed.

## Configuration

Linux x86-64, Node v22.22.3, GCC 12.2.0, base revision
`68f301600c72b442898313aeb49d57be0baac2f0` plus the uncommitted changes described in the
[Fluid Path plan](../../../Documentation/Plans/Ongoing/FluidPath.md). Tests used Chromium 141 and SwiftShader 5.0.0,
a **CPU software Vulkan device**, not a physical GPU. Reproduction commands are below.

## Recovered Experimental/Fluid editor

- `npm ci`, `npm test` and `npm run build` succeeded with Node v22.22.3; 22 scene/unit tests passed.
- The editor was served by the shared launcher, then tested with Chromium 141 and SwiftShader WebGL2: 18 browser checks
  passed, including actual shader execution, finite nonzero voxel data, single stepping, settings changes, scene
  import/export, grid reallocation, preset search, camera, layout and error capture.
- The browser had no WebGPU adapter and the editor correctly fell back to WebGL2. The WebGPU engine, physical hardware,
  numerical solver accuracy and production Vulkan integration are **not** verified by this run.
- The upstream browser test initially expected 10 presets while the editor now has 18; its assertion was updated to
  match the preset registry. See [browser log](RecoveredFluidBrowser.txt) and
  [captured workspace](../Images/VerifiedWorkspace.png). The screenshot is paused after a reset.
- Font URLs were adjusted to reuse the licensed local Vehicle HMI DM Sans files so both the shared server and Vite
  can serve them without the absent upstream font directory.

Reproduce with `npm ci && npm test && npm run build` in `Experimental/Fluid`. With Playwright Chromium installed,
start the shared server and run `FLUID_URL=http://127.0.0.1:8000/Experimental/Fluid/ npm run verify:browser` there.

## Earlier reference results

| Check | Result | Scope |
| ----- | ------ | ----- |
| Vehicle HMI tests | 237 passed | JS unit/integration tests |
| Particle Editor tests | 45 + 91 + 44 + 65 assertions passed | Includes jsdom card-shape checks |
| Liquid pressure routing | Odd/even substeps, visualization, reset and reallocation passed | Actual host function under mocked command recording; the original odd-substep sequence failed this test before the repair |
| Liquid numerical compute | 1, 2, 40 and 100 sweeps match a CPU transcription within 1.91e-6 | Executed actual WGSL on SwiftShader; synthetic 8³ scene with solid walls and air boundary |
| Atomic particle scatter | 1,024 particles concentrated into one cell; all 8 weights, momenta and marker exact | Actual WGSL on software Vulkan; not an overflow stress proof |
| Plume self-attenuation | Four uniform clear/smoke/soot cases match analytic optical depth and exponential | Extracted actual `ResolveOpticalDepth` and shared coordinates; not the full scene-shadow renderer |
| Vehicle plume integration | Active sources and transport steps plus march resources; zero WebGPU errors/warnings | Actual host on software Vulkan, 3 frames; does not assert shadow image quality |
| Volumetric module syntax | Embedded module parses | A browser run exposed unescaped backticks in shader comments before correction |
| Browser launch and interaction | Liquid, particle, vehicle and gas rendered with no captured console/page errors | Chromium 141 + SwiftShader; liquid 32³ resolution/pressure selection/step/reset, particle pause/wind arrows; gas limited to 2 frames |
| Liquid WGSL | 11 modules compiled and pipelines created on Dawn null | Null validation; separate numeric tests above execute on software Vulkan |
| Native cell addressing | 517 JS/C++ comparisons and 2 rejected invalid inputs | GCC C++17 ASan/UBSan; CPU addressing only |

[Liquid compute log](LiquidCompute.txt) · [pressure routing log](PressureRouting.txt) ·
[plume optics log](PlumeOptics.txt) · [plume integration log](PlumeIntegration.txt)

| Actual browser capture | Notes |
| ---------------------- | ----- |
| [Fluid Editor](../Images/fluid.png) | 32³ after resolution, pressure selection, step and reset |
| [Particle Editor](../Images/particle.png) | Pause/resume and wind arrows exercised |
| [Vehicle HMI](../Images/vehicle.png) | Low quality / 25% rendering scale; not a quality or speed measurement |
| [Volumetric Flow](../Images/gas.png) | Two scheduled frames; not an image-quality acceptance |

## Remaining projection defect

The liquid pressure solve's nearest-neighbour Laplacian is not the composition of its central divergence and central
pressure gradient. The GPU/CPU comparison confirms current behaviour, not correctness of incompressibility:

| Jacobi sweeps | Initial divergence L2 | Post-projection L2 | Maximum GPU/CPU absolute difference |
| ------------- | --------------------- | ------------------ | ----------------------------------- |
| 1             | 16.492142             | 15.293278          | 2.384e-7                            |
| 2             | 16.492142             | 14.301398          | 2.384e-7                            |
| 40            | 16.492142             | 8.609374           | 9.537e-7                            |
| 100           | 16.492142             | 7.870682           | 1.907e-6                            |

L2 is over the fluid cells of this fixture; it is not a general convergence or performance benchmark. The stencil
mismatch remains open. A consistent operator and boundary design must be validated before production porting.

## Reproduction

The browser and SwiftShader binaries are external validation prerequisites; they are installed in `/tmp`, not vendored.
The pinned package includes third-party software; see its npm licence. Commands below reproduce the tested Linux setup.

```sh
npm --prefix Experimental/VehicleHmiPrototype ci
npm --prefix Experimental/VehicleHmiPrototype/Tools ci
npm --prefix Experimental/ParticleEditor ci
npm install --prefix /tmp/frontier-browser --no-audit --no-fund \
    @sparticuz/chromium@141.0.0 puppeteer-core@24.21.0
node Exhibits/FluidPath/Code/PrepareSoftwareDevice.mjs
node Exhibits/FluidPath/Code/PressureRoutingValidation.mjs
node Exhibits/FluidPath/Code/VolumetricSyntaxValidation.mjs
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json \
    node Exhibits/FluidPath/Code/LiquidComputeValidation.mjs
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json \
    node Exhibits/FluidPath/Code/PlumeOpticsValidation.mjs
(cd Experimental/VehicleHmiPrototype && \
    LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json \
    BACKEND=vulkan SIZE=160x90 HARNESS_TIMEOUT=450000 \
    HOOKS=../../Exhibits/FluidPath/Code/PlumeIntegrationValidation.mjs \
    node Tools/HeadlessHarness.mjs . 3 '?capture=1&quality=low&stresspool=0')
python3 Experimental/FluidPath/PrepareReferences.py
python3 Experimental/FluidPath/ServeReferences.py  # keep running in another terminal
for TARGET in fluid particle vehicle gas; do
    BROWSER_TARGET=$TARGET node Exhibits/FluidPath/Code/BrowserValidation.mjs
done
```

For the original CPU, npm and null-backend checks, see the [plan](../../../Documentation/Plans/Ongoing/FluidPath.md).
Browser captures use the test program's explicit SwiftShader environment and low settings. The gas test caps animation
to two frames to avoid queuing unbounded software-GPU work.

## Not verified

- Windows, hardware GPU, performance, final scene-shadow visuals and shared force/particle/fluid coupling.
- Full liquid simulation numerical equivalence, mass conservation, moving collider boundaries, dense overflow, and
  the current projection's physical correctness. The shadow test only covers a uniform extracted optical-depth probe.
- Production C++/Vulkan fluid integration does not yet exist in this checkout. The browser references are not evidence
  of production editor parity.
