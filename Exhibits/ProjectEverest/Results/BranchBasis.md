# Source-branch basis decision

## Decision

Use a **split, role-specific basis** rather than merging one research branch wholesale:

1. **UI, browser/host composition, shared theme and docking:** SultanAladin/Frontier, `arena/11f31835-frontier`, inspected at `71e4a17ed543325061b6e5f392476f5c9fd26699`.
2. **LogStream engine/panel work:** egoistm1inbox/Frontier, `arena/9ce908bc-frontier`, inspected at `f97b991440fc90476383ba3f5cf124addc590f18`.
3. **ReSTIR GI experiments:** streamlinkinbox/Frontier, `arena/08e7de48-frontier`, inspected at `7bc60250e153a9b7d70c9740744723fde19fae22`.
4. **Ray-query/software-BVH and temporal-visibility audits:** streamlinkinbox/Frontier, `arena/5ec53579-frontier`, inspected at `56059b393f2d1a6f4ecf73c0f7661aa74f2de442`.

The SultanAladin branch is the best basis for the immediate UI phase because it contains the production Control Centre, shared controls/theme, dockspace, and `.frontier` host flow. Keep its `.frontier` project flow separate from the `.projectspace` catalogue. Use the egoistm1inbox branch for the LogStream implementation, not as the UI/theme source. Keep the two streamlinkinbox research branches as evidence/reference inputs rather than treating either as the production renderer base.

## Evidence and limits

| Branch | Evidence inspected | What it supports | What remains unproven |
|---|---|---|---|
| `arena/11f31835-frontier` | `Frontier/Applications/Editor/FrontierMain.cpp` (`RunBrowserWindow`), `ProjectBrowserHost`, `ProjectDescriptor`, `ControlCentrePanel`, `PanelDockspace`, `NativeWorkspace.png` | Existing `.frontier` project list and browser-to-workspace path; shared Control Centre/docking; Outliner + Viewport placeholder + LogStream composition; Project Everest native CPU-rasterized workspace capture | Project-authored vehicle Inspector; renderer integration |
| `arena/9ce908bc-frontier` | Project-Drive Vulkan sources, `LogStreamPanel`, native workspace capture/compile proof, GTX/RTX path references | Native LogStream work and Vulkan integration reference | The cited shader run is blocked by missing `slangc`; no physical RT-core run, reproducible hardware timings, or complete swapchain presentation proof |
| `arena/08e7de48-frontier` | ReSTIR GI and ControlVehicle exhibit-only studies | ReSTIR GI sampling/lighting research inputs | Production C++ Vulkan renderer or validated GPU execution |
| `arena/5ec53579-frontier` | `Exhibits/LightCompute/ReplayRayAcceleration.md`, ray-query and ordered SAH-2 software-BVH studies | A ray-query/software-BVH path; the report includes a SwiftShader BVH execution path | RTX hardware RT-core performance; GTX fallback correctness; proof that SDF plus BVH reflections work on physical GTX hardware |

The requested tiering remains an intent, not a verified deliverable: RTX-capable/high-end GPUs should use hardware ray tracing; lower-end GTX GPUs should use SDF with BVH reflections. Do not claim either physical hardware path works until the relevant code compiles, the shaders run on target hardware, and reproducible captures/results are retained. Native UI translation units, Project Everest descriptor/record checks, and CPU-rasterized native ImGui captures now pass locally; no GLFW OS window, Vulkan renderer, Windows runtime, or GPU execution was performed for this UI exhibit.

## Post-commit UI-upstream sync check

After the initial native UI commit, fetched `SultanAladin/Frontier` branch `arena/11f31835-frontier` at the pinned `71e4a17ed543325061b6e5f392476f5c9fd26699`. The session checkout and UI source have independent Git roots; after unshallowing the session's `origin` history, `git merge-base` still returned no common ancestor. No unrelated-history merge was forced. The selected shared modules (`Engine/Interface/{ControlKit,PanelDockspace}`, `UI/Editor/{ControlCentrePanel,ContentBrowserPanel}`, and the pinned UI setup/ImGui patch files) compare unchanged against the fetched UI ref. The ref SHA remains recorded in `Frontier/Projects/Project-Everest/Upstream.json`.

On 2026-10-31, re-read `RunBrowserWindow` at that same UI ref and restored its original inline `.frontier` list body; no browser helper, alternate chooser, or capture-only browser presentation remains. The `.projectspace` content catalogue remains separate. The custom VehicleHMI SVG source files were copied unchanged from the pinned UI ref; the FNV-1a source markers for the six ThorVG vector variants were verified against their source SVGs. The native Sun Inspector now has the VehicleHMI Overview, Daylight, Light & colour, and Disc & optics sections, with sliders delegated to the pinned shared `ControlKit`/`PixelSpace`. No Git merge was attempted because the roots remain unrelated.
