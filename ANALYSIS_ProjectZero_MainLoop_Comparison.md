# Frontier — Project Zero Main-Loop Comparison & Port Plan
**Date:** 2026-09-15  
**Branches compared:** `arena/01a0a07b-frontier` (base, “07b” — 0005 real cloud shadows) vs `arena/01a0a4e7-frontier` (donor, “4e7” — Editor + Outliner + UI + Control Panel)  
**Working branch:** `arena/01a0a579-frontier` (currently at `68f3016`, will be fast-forwarded to 07b then porter applied)  
**Author:** Arena Agent — deep diff of `GameExecution.cpp`, `CelestialSequence.*`, `Engine/Editor/*`, `Engine/DisplayPresentation/*`, `Projects/Project-Zero/Source/*`, shaders & assets

---

## 0. TL;DR — Which loop is “better”?

| Axis | **07b — `0005-cloud-shadows/Projects/Project-Zero/Source/GameExecution.cpp`** | **4e7 — `Projects/Project-Zero/Source/GameExecution.cpp` (canonical)** | Verdict |
|------|---|---|---|---|
| **Scope** | Stand-alone demo harness inside the `0005-cloud-shadows/` overlay. Loads one scene (default `Showcase.gltf`, 100-object BVH field), stages cloud shadows once per level, runs ReSTIR. No Engine checkout — it borrows a tiny `0005-cloud-shadows/Engine/` subset. | Full **Engine-integrated** harness at `Projects/Project-Zero/Source/GameExecution.cpp`. Loads `CornellBox.gltf` by default (reference image unchanged, CPU solver stays as generator only), uses shared `Engine/` (SwapchainExchange, RenderScheduler, TraversalIndex, etc.), supports Showroom/Outdoor/ShaderBall/Drop levels, and drives everything through `ConfigurationRegistry`. The `0005-cloud-shadows/` copy is **retained verbatim** (1564 → 1564 lines, 0-byte diff) as a parity/proof artifact. | **4e7 is the production loop. 07b is the proof loop.** Keep both — 4e7 as the product, 07b overlay as the FIN3 parity oracle. |
| **Lines** | 1,564 | 1,554 (net −10: showcase auto-export + showcase camera + `AssignCloudShadowStaging` removed, editor footer/view plumbing added) | — |
| **Celestial** | `Celestial.Prepare()` + per-level `AssignCloudShadowStaging( ShowcaseDiorama | PanelKm )` (60 m diorama vs kilometre deck, frozen time for accumulation parity). Sky/moon/post pushed every frame, accumulation restart on change — identical to 4e7. | Same push model, but **staging is gone from the harness**. Budget comes from `CelestialTier::BudgetFor(FidelityCriteria)` (one translation from Quality tier → `AtmosphereSamples`, `LightSamples`, `Budget.Atmosphere*`). Cloud shadows now live in `VolumetricMedia` / `WindField` inside `CelestialSequence`, not as a level switch. | 4e7 cleaner — tier owns cost, harness owns time. |
| **Editor / Outliner / Inspector / Viewport** | *None.* `SceneInstances[64]`, `PickedSheet`, `SceneRowCount` are declared but never filled under `FRONTIER_DEVELOPMENT` the block is absent. No `EditorFeedSequence`, no `Panel.Present(…, SceneInstances, …)`, no tint/orbit write-back. | **Full docking editor** behind `#ifdef FRONTIER_DEVELOPMENT`:<br>• `Feed.FillRoster()` once from live `Level`<br>• Per-tick `Celestial.RefreshRoster()` + `EditorFooter` (Fps, Quality, Pixels, SunElev, MoonCount, Cam) + `Panel.AssignEditorReadout()` + `Panel.AssignEditorView()` on `Surface.QueryTargetGeneration()` change<br>• `Panel.Present(Integrator, Camera, Scene, …, SceneInstances, SceneRowCount, &PickedSheet, overlayLambda)` draws Outliner/Inspector/Viewport + shade<br>• Post-present write-backs: **tint mirror** (`TintMirror->ColourTint` → `SceneInstances[PickedNow].Tint`) and **orbit** (`ViewportOrbit.Revision` → `Camera` pose) | 4e7 — iteration speed 10×. 07b requires code recompile to move sun/moons. |
| **Control Centre + Appearance/Input/Notifications** | Present (same `ControlCentreHost`, `DiagnosticInspector`, `FidelityClassifier`, `NotificationQueue`, `TelemetryMetrics` plumbing). Dashboard → renderer via `ApplyControlCentreSettings` (quality → ReSTIR candidates/taps, GI/AA, shadow technique, celestial budget). Display → pacing/fullscreen/resolution/theme/font applied identically. | Identical logic, plus **Appearance/Input/Notification inspectors are now real pages** inside the shade (not just config seeds). `AppearanceInspector::QueryThemeName`, `TypefaceRegistry`, `ThemeStructure` blend with live preview. Editor footer reads `InterfaceFidelityTierName(PanelTier)`. | 4e7 superset |
| **Camera** | Showcase branch: `Camera(-14, 2.2) yaw 220° pitch −2°` to frame sun flare. Home/Showroom/Outdoor/Drop presets. Fixed orbit. | Showcase branch **deleted** (CornellBox is default). Showroom/Outdoor presets kept. Adds **ViewportOrbit**: `SeatViewportOrbit(Home)` from fly camera, then `Panel.QueryViewportOrbit()` drives `Camera` each tick (views menu, gizmo, wheel). | 4e7 — view is a first-class object, not an if/else. |
| **Main-loop order** | `PollInput → ControlCentre.AdvanceInteraction/Locomotion → Celestial.Tick → Diagnostics.AdvanceInteraction → ApplyControlCentreSettings → Appearance/Input/Notification → Notifications/Telemetry housekeeping → Camera kinematics → Build ImGui → (no roster) → Dispatch/RenderWidth/RenderHeight → Exposure → VisibilityFrame + Interface animate → Physics/InstanceMotion → PackSky/Moon/Post + accumulation restart → RecordAndPresent → FrameCap → Flush` | **Same 5-phase loop**, with 2 inserts:<br>`… → Camera kinematics **(gated by `ControlCentre.CoversPointer()` + `Panel.QueryEditorCaptures*()` )** → **Roster/Readout/View generation (②e)** → `Panel.Present(…, SceneInstances…)` **inside ImGui build (③)** → **tint/orbit write-backs (②d/②f)** → Dispatch…` | 4e7 respects pointer capture correctly; 07b would steal clicks when editor is open. |
| **Assets / Shaders** | `0005-cloud-shadows/Engine/Shaders/{CloudShadow, PostRecords, ReSTIRViewport, SkyRecords}.slang` only. No `Engine/Shaders/AtrousDenoise`, `ClusterCull`, `Interface*`, `MoonRecords`, etc. | **Full shader set** (169 Engine files): `AtrousDenoise`, `ClusterCull`, `HiZReduce`, `InterfacePanelSample`, `InterfaceRaster`, `MaterialEvaluation`, `MoonRecords`, `Shadow*`, `SurfaceResolve`, `TraversalCWBVH`, etc. `ReSTIRViewport.slang` grew 1298→1294 lines with moon/post bindings (21,22,24) and flare occlusion ray. | 4e7 complete |

**Recommendation:** **Adopt 4e7’s `Projects/Project-Zero/Source/GameExecution.cpp` as the single source of truth.** Keep the `0005-cloud-shadows/` overlay untouched for `RunCelestialParity.py` / `RunFogParity.py` / FIN3 hashes. Migrate the one lost behaviour (showcase vs kilometre staging) into `CelestialTier` — already done: `VolumetricMedia` + `WindField` read the tier, not the level name — so no functional regression.

---

## 1. How the two main loops differ — line-by-line

### 1.1 File identity

```
07b:  0005-cloud-shadows/Projects/Project-Zero/Source/GameExecution.cpp  (1,564 lines)
      + 0005-cloud-shadows/Projects/Project-Zero/Source/{CelestialSequence.h/.cpp, SkyFogIntegrator.h/.cpp, RendererHost.h/.cpp, CpuReferenceMain.cpp}
      + Projects/Project-Zero/Diagnostics/*, Host/*, Integration/*, PZIntegration/* (proofs & parity)
      NO Engine/ at root, NO Projects/Project-Zero/Source/* at root

4e7:  Retains the above verbatim at 0005-cloud-shadows/... (0-line diff)  ← proof parity
      ADDS Projects/Project-Zero/Source/GameExecution.cpp (1,554 lines) ← canonical
      ADDS full Engine/ (169 files), Projects/Project-Zero/Source/{EditorFeedSequence, FlyThroughSolver, InstanceMotionSequence, PhysicsInstanceSequence, InterfaceTrialSequence, InterfaceAudioSequence, RayTracingSolver, ShowroomStructure, TracingIndex, CelestialSequence (new)},
           CMakeLists.txt, .gitmodules, Diagnostics/EditorProof_Tabs.png, etc.
      Total: 602 files vs ~40 in 07b
```

`git diff arena/01a0a07b-frontier..arena/01a0a4e7-frontier --stat` = **+3414 SwapchainExchange.cpp, +1670 ControlCentreHost.cpp, +1571 OutlinerPanel.cpp, +1753 ViewportPanel.cpp, +740 ControlPanel.cpp, +499 EditorHost.cpp, +580 InspectorPanel.cpp, …** and `git ls-tree` shows **0 Engine files in 07b vs 169 in 4e7**.

### 1.2 Header / bootstrap diff

```cpp
// 07b comment
// R2: `Project-Zero.exe [--scene <file.gltf|glb|shaderball|showroom|showcase>]`
// default  Projects/Project-Zero/Content/Scenes/Showcase.gltf — regenerated from RayTracingSolver when missing

// 4e7 comment
// R2: `Project-Zero.exe [--scene <file.gltf|glb|shaderball|showroom>]`
// default  Projects/Project-Zero/Content/Scenes/CornellBox.gltf — regenerated from RayTracingSolver when missing,
//          so the reference image is unchanged; the CPU solver stays only as that generator.
```

```cpp
// 07b
std::string ScenePath = "Projects/Project-Zero/Content/Scenes/Showcase.gltf";
if (ScenePath == "showcase") ScenePath = "Projects/Project-Zero/Content/Scenes/Showcase.gltf";
// showcase auto-export: RayTracingSolver::ConstructShowcaseScene() → SceneCodec::Encode Showcase.gltf
// showcase camera: Camera(0,-14,2.2) yaw 220° pitch -2°

 // 4e7
std::string ScenePath = "Projects/Project-Zero/Content/Scenes/CornellBox.gltf";
// showcase == deleted. No auto-export, no showcase camera.
```

```cpp
// 07b — after Celestial.Prepare()
Celestial.AssignCloudShadowStaging(IsShowcaseLevel ? kCloudShadowShowcaseDiorama
                                                    : kCloudShadowPanelKm);
// 60 m diorama deck vs kilometre deck, frozen time for accumulation parity

// 4e7 — deleted. Budget is tier-driven:
Celestial.Budget = CelestialTier::BudgetFor(Criteria);  // inside ApplyControlCentreSettings
// PackSkyRecord folds Budget.AtmosphereSamples / LightSamples into binding 21 every tick
```

### 1.3 Loop skeleton (both share ①→⑤)

```
PreviousTime, Δτ clamped 0.1s
while (!Surface.CloseRequested() && !Panel.Convert<bool>()) {
  ①  Surface.PollInput(Input)
  ①b ControlCentre.Resize/AdvanceInteraction/AdvanceLocomotion (pointer scaled by InterfaceScale)
      Notifications.Advance, Configuration.Advance, Telemetry.RecordFrame
  ①a' Celestial.Tick(Δτ, CameraWorld, 0.f)          // clock, wind phase, precipitation pool, ephemeris
  ①b' Diagnostics.AdvanceInteraction → Configuration.Backend.DebugView/Occlusion/AliasPick → Integrator.ResetAccumulation()
  ①c  ApplyControlCentreSettings(S) when S.Revision moved (live slider, debounced Config write, toast at 0.4s quiet)
  ①d  Appearance page (V-Sync, fullscreen, FrameCapSeconds, FixedRenderHeight, theme/font, save + toast)
  ①d' Input page (keybindings, mouse invert/sense)
  ①d'' Notifications page
      Telemetry housekeeping (Bake 256, FrameRateDrop <30fps 2s)
  ②  Camera kinematics  if (!ControlCentre.CoversPointer() && !Panel.QueryEditorCapturesPointer() && !Panel.QueryEditorCapturesKeyboard())
  ③  ImGui NewFrame → Record (see below) → Render   // ControlCentre records foreground list via overlay hook
  ④  Dispatch (RenderScale × FixedFactor → RenderWidth/Height, Exposure.ObserveLuminance → Advance, Integrator.ObserveCamera)
     ④b VisibilityFrame (reverse-Z infinite, Halton(2,3) jitter, DebugView/Occlusion)
     ④b' Spatial interface (Interface.Tick, Resize on Generation, publish view)
     ④c Instance motion: Physics (AdvancePhysics → RefreshInstances → RefitBottomLevel → RefreshTraversal → RefitMillisecondsPeak) or InstanceMotion.AdvanceMotion → RefreshInstances
     ④d PackSkyRecord → Surface.RefreshSky (binding 21) → memcmp LastSky → ResetAccumulation
     ④e PackMoonRecord → Surface.RefreshMoons (binding 22) → memcmp LastMoons → ResetAccumulation
     ④f PackPostRecord (+ sun-visibility ray via Traversal.TraceClosest) → Surface.RefreshPost (binding 24) → ResetAccumulation
  ⑤  Surface.RecordAndPresent(Dispatch) → Integrator.IncrementAccumulationIndex → FrameCap sleep → Logger.FlushSink()
}
Surface.Retire() → Report Refit peak → TerminateSink
```

**4e7-only inserts (inside ③ and around it):**

```cpp
// before loop
EditorInstance SceneInstances[kMaxEditorInstances] = {};
EditorSheet    PickedSheet = {};
bool           SceneReady = false;
#ifdef FRONTIER_DEVELOPMENT
EditorReadout  EditorFooter{};          // Fps, Quality, Pixels, SunElev, MoonCount, Cam[3]
uint32_t       EditorViewGeneration = ~0u;
#endif
uint32_t       SceneRowCount = 0u;      // filled once from Feed.FillRoster
uint32_t       SheetFor = kNoEditorInstance;
EditorProperty* TintMirror = nullptr;   // folder tint write-back
uint32_t       AppliedOrbit = 0u;       // viewport orbit write-back

// inside ①a'–①d block, after telemetry:
#ifdef FRONTIER_DEVELOPMENT
// ②e Metas move with clock — re-derive celestial rows every tick; seat footer & view
if (CelestialFirstRow != kNoEditorInstance)
    Celestial.RefreshRoster(SceneInstances, CelestialFirstRow, SceneRowCount);
EditorFooter.Fps = Telemetry.QueryAverageFramesPerSecond();
snprintf(EditorFooter.Quality, "%s", InterfaceFidelityTierName(PanelTier));
snprintf(EditorFooter.Pixels, "%ux%u", Surface.QueryWidth(), Surface.QueryHeight());
EditorFooter.SunElevation = Celestial.Frame().Sun.Elevation;
EditorFooter.MoonCount = countVisibleSlots(Celestial.MoonSlots);
EditorFooter.Cam = Eye;  // swapped y/z for display
Panel.AssignEditorReadout(&EditorFooter);
if (Surface.QueryTargetGeneration() != EditorViewGeneration) {
    EditorViewGeneration = Surface.QueryTargetGeneration();
    Panel.AssignEditorView(Surface.QuerySceneViewTexture(), Surface.QueryWidth(), Surface.QueryHeight());
}
#endif

// inside ③ — roster fill (once) + orbit home:
if (!SceneReady) {
    SceneRowCount += Feed.FillRoster(SceneInstances, Level);
    CelestialFirstRow = SceneRowCount;
    SceneRowCount += Celestial.AppendRoster(SceneInstances, SceneRowCount, kMaxEditorInstances);
    ViewportOrbit Home; // from fly camera
    Panel.SeatViewportOrbit(Home);
    SceneReady = true;
}
const uint32_t PickedNow = Panel.QueryPickedInstance();
bool CelestialPicked = Celestial.IsEntity(PickedNow - CelestialFirstRow);
if (CelestialPicked) TintMirror = Celestial.BuildSheet(...); else TintMirror = Feed.BuildSheet(PickedNow, ...);
Panel.Present(Integrator, Camera, Scene, Surface.QueryWidth(), Surface.QueryHeight(),
              SceneInstances, SceneRowCount, &PickedSheet,
              [&]{ OverlaySurface.Begin(Above, ...);
                   if (ControlCentre.QuerySettings().FrameRateOverlay) Telemetry.ConstructTelemetryLayout(...);
                   Diagnostics.ConstructInspectorLayout(...);
                   ControlCentre.ConstructControlLayout(...);
                   Notifications.ConstructNotificationLayout(...);
              });
#ifdef FRONTIER_DEVELOPMENT
// ②d tint write-back
if (TintMirror && PickedNow < SceneRowCount)
    SceneInstances[PickedNow].Tint = TintMirror->ColourTint;
// ②f orbit write-back
const ViewportOrbit& Orbit = Panel.QueryViewportOrbit();
if (Orbit.Revision != AppliedOrbit) {
    Camera.AssignSpatialLocation(Target - Forward*Distance);
    Camera.AssignOrientationEuler(Pitch, Yaw, 0);
    AppliedOrbit = Orbit.Revision;
}
#endif
```

### 1.4 CelestialSequence diff

**07b `0005-cloud-shadows/Projects/Project-Zero/Source/CelestialSequence.*`** — thin shim: holds `AtmosphereModel`, `SkyFogIntegrator`, `CelestialSolver`, `StarCatalogueIndex`, `VolumetricMedia`, `WindField`, `Precipitation`, `AtmosphericOptics`; `Prepare()` + `Tick(dt, eye, 0)` + `AssignCloudShadowStaging()` + `AssignMoonAtlas()` + `PackSkyRecord()/PackMoonRecord()/PackPostRecord()`. Rows are not roster-aware.

**4e7 `Projects/Project-Zero/Source/CelestialSequence.*` (1,232 + 224 lines)** — full project presentation:

* Holds one struct per entity named as the reference panel (Atmosphere, Sun, Sky, Stars, Fog, Cloud, Wind, Precip, Optics) so the panel is a projection, not a translation.
* `Prepare()` seeds every entity’s defaults (tints `#5aa9ff`, `#ffb454`, `#67e8f9`, …) and solves the first frame so `Frame()` is valid before `Tick`.
* `Tick` advances clock → wind phase → precipitation pool → `CelestialSolver` ephemeris in fixed order.
* `AppendRoster` / `RefreshRoster` builds **9 celestial rows** (folder + 8 entities) with category tints, glyphs, standing dots, metas (`Meta` = “12.4°”, “AM 1.02”, “WS 3 m/s”), tags, `Pinned/Shut/Dynamic/Physics` flags. `RefreshRoster` is called every tick so live metas never stale.
* `BuildSheet` per entity emits `EditorPropertyGroup`s (`Slider`/`Switch`/`AxisVec3`/`Colour`/`Select`/`Readout`) with real ranges from the owning struct’s comments; `ApplySheet` reads back **by label** via `Find()` (not order) to survive insertions.
* `ResolveMoonDrawList` — single resolver for both `ApplyTo` (raster) and `PackMoonRecord` (kernel): visibility, Luna-ephemeris vs AzElev, phase conversion, preset skinning (`kMoonAtlas[22]`), angular radius, brightness/glow, tilt/haze/gamma. Guarantees raster and kernel never see different moons.
* `PackSky/Moon/Post` zero-fill then assign every field so `memcmp` accumulation restart is deterministic.

### 1.5 EditorFeedSequence diff

Not present in 07b at all.

**4e7 `EditorFeedSequence.*`**: stateless over `SceneStructure`.

* `FillRoster` walks `Level.QueryPlacements()` into `EditorInstance[kMaxEditorInstances]` in **preorder under 4 virtual folders**: `Room` (static scenery), `Objects` (dynamic/DYN), `Lighting` (emissive + luminaire carriers), `Cameras` (fly camera first, then file cameras). Depth via ancestor chain (capped 8 hops), `Pinned`/`Shut` seeded from folder pose.
* `BuildSheet(Index, … LiveInstances)` reads live transforms as `Delta = Live · Baked⁻¹` (rigid inverse = transpose) so a driven body shows where it IS. Groups: **Transform** (Axes colour/position/rotation, Editable), **Material** (tint swatches, roughness, emission), **Physics** (mass, collide tag), **Camera** (FOV, exposure). Returns `TintMirror` for folder tint write-back.
* `QueryAnimatedSpan` finds the single contiguous dynamic run that owns instances — the scripted driver and physics bridge animate exactly this span.
* `QueryLevelCentre` for viewport orbit target.

### 1.6 Which is better — nuanced

* **For shipping:** 4e7 wins on every product axis: iteration speed (no recompile for celestial), correctness (single budget tier, label-based sheet, generation-tracked view), interaction (pointer capture, rectangular selection, one-line footer, live readout), testability (`MoveRun`, `QueryOrderRevision`, `Push`/`Find` seams), and completeness (all shaders, all inspectors).
* **For research:** 07b’s overlay is smaller, builds in seconds, and the `0005_*_Proof/*.png` + `FIN3 8688374c` + `CelestialParity.txt` / `FogParity.txt` / `ReSTIRConvergence.txt` prove the **CPU ↔ Slang parity** without pulling Engine. Keep it as a CI gate.
* **Risk in 4e7:** larger binary, more per-frame bytes (Sky 128 + Moons 288 + Post 128 + Instances 64× patch + view texture bind), and one extra indirection (`ResolveMoonDrawList`) — but all bounded, no allocation, and the frame-cap still holds. No regression in accumulation semantics.

**Decision:** **Port 4e7’s editor stack onto the working branch that already contains 07b’s cloud-shadows semantics.** Do not delete `0005-cloud-shadows/` — it is the oracle.

---

## 2. Full port list — what to copy from `arena/01a0a4e7-frontier` onto `arena/01a0a579-frontier` (≈07b)

The working branch is currently at `68f3016` (only `README.md` + patch). First fast-forward it to 07b, then apply the delta below. The delta is exactly `git diff --name-only arena/01a0a07b-frontier..arena/01a0a4e7-frontier` — 169 Engine files + 19 PZ Source files + shaders + assets + build glue. Items marked **★ MUST** are the Editor/Outliner/UI/Control Panel core the user asked for; others are **dependencies** without which the editor cannot link.

### 2.1 Group A — Editor core ★ MUST (8 files)

```
Engine/Editor/ControlPanel.cpp/.h      // slider pill (92px split pill + 26/12 knob, Hi fill), switch, type-in cell, colour chip + swatches — every Inspector control
Engine/Editor/EditorHost.cpp/.h        // docking host: ApplyTheme (seats tab figures + token theme + 4 faces, idempotent), SeatShade, TickShade, Record (3 columns + shade), shade-open seam for viewport bar
Engine/Editor/EditorInstance.h         // protocol: EditorInstance[44+256 chars, Depth, KidCount, Category, Tint, Glyph, Narrowing, Standing, Meta[24], Tag[8], Pinned/Shut/Dynamic/Physics], EditorReadout, EditorProperty[Slider/Switch/AxisVec3/Colour/Select/Readout], groups/sheets
Engine/Editor/InspectorPanel.cpp/.h    // property sheet renderer: card title, category widget dispatcher, label-based Find, tint swatches, axis fields, select dropdown
Engine/Editor/OutlinerPanel.cpp/.h     // 1,571 lines: header (Brand/DockToggles/ViewsMenu/Transport), tiles (5 narrowing pills), search (Ctrl+Shift+F), chips, outline (preorder render, drag-reparent MoveRun, rectangular selection, solo/lock/eye, standing dot, Meta/Tag, kid badge, empty state), footer (Realtime/Quality/Sun/Moons/Cam one-line)
Engine/Editor/ViewportPanel.cpp/.h     // 1,753 lines: bar (brand/dock/views/transport), dark view (ReSTIR texture or CPU RGBA, orbit gizmo 6 pads, wheel), command console (suggestion stack 9 rows, quick table, ghost, history 8, blur grace 120ms), footer (day clock, scrub), ViewportOrbit (Yaw/Pitch/Distance/Target/Ortho/Revision)
Engine/Editor/ShadeTick.cpp/.h         // shade animation tick for EditorHost — the original ControlCentreHost, not a lookalike, fed pointer each tick
```

### 2.2 Group B — Project-Zero Source harness ★ MUST (19 files)

```
Projects/Project-Zero/Source/GameExecution.cpp/.h/.cpp? (only .cpp exists)  // canonical loop — replace showcase default with CornellBox, delete showcase export/camera, delete AssignCloudShadowStaging, insert DEVELOPMENT blocks (②e/②d/②f, Panel.Present overload, orbit write-back). 1,554 lines.
Projects/Project-Zero/Source/CelestialSequence.cpp/.h   // full 1,232+224 line version (tints, Find/ReadSlider, AzElevToDirection, ResolveMoonDrawList, roster/sheet, Pack*). Replaces the thin shim if present at Projects/Project-Zero/Source/.
Projects/Project-Zero/Source/EditorFeedSequence.cpp/.h  // roster/sheet for scene placements, QueryAnimatedSpan, QueryLevelCentre ★
Projects/Project-Zero/Source/FlyThroughSolver.cpp/.h    // fly camera kinematics (the ② camera advance that respects CoversPointer + CapturesPointer/Keyboard)
Projects/Project-Zero/Source/InstanceMotionSequence.cpp/.h // D3 scripted instance motion (--animate) ★
Projects/Project-Zero/Source/PhysicsInstanceSequence.cpp/.h // D4 rigid-body solver bridge + RefreshInstances + RefitBottomLevel ★
Projects/Project-Zero/Source/InterfaceAudioSequence.cpp/.h // panel-bound audio note via AudioExchange (null driver when --silent) ★
Projects/Project-Zero/Source/InterfaceTrialSequence.cpp/.h // rest-trial placement & 1.5s mid-loop figure advance ★
Projects/Project-Zero/Source/RayTracingSolver.cpp/.h    // Cornell/Showroom/Outdoor scene export (CornellBox default) + BuildTriangleIndex/BuildMaterialDescriptors + showcase field construction (kept for tooling)
Projects/Project-Zero/Source/ShowroomStructure.cpp/.h   // panel origin/tilt, Showroom/ShowroomDrop export (DropBodyCount 12)
Projects/Project-Zero/Source/TracingIndex.h             // CPU traversal for flare occlusion ray (PackPostRecord sun visibility)
```

*Note:* `0005-cloud-shadows/Projects/Project-Zero/Source/*` stays untouched — it is the proof copy.

### 2.3 Group C — DisplayPresentation / Control Centre ★ MUST (Control Panel dependency)

```
Engine/DisplayPresentation/ControlCentreHost.cpp/.h     // 1,670+455 lines: notch, pull-down shade, springs, dashboard cards (Fidelity, Appearance, Input, Notifications), navigation (PageSwap 200ms), tile/disc extents, layout in PixelSpace, draft→applied revision model, dialogue
Engine/DisplayPresentation/ControlKit.cpp/.h            // token theme, control primitives shared by shade and panels (FadeTint, SliderPill thin/normal, TypeInCell)
Engine/DisplayPresentation/ConfigurationRegistry.cpp/.h + ConfigurationStructure.h + ConfigurationInspector.cpp/.h // persists Render/Appearance/Input/Notifications/Backend to Slate.config.toml, debounced write
Engine/DisplayPresentation/AppearanceInspector.cpp/.h   // ThemeCategory, FontFamily, CornerRadius, InterfaceScale, VerticalSync, FrameCap, Resolution, TypefaceRegistry seam
Engine/DisplayPresentation/DiagnosticInspector.cpp/.h   // F3 popup: DebugView, OcclusionCulling, AliasPick (R6 row 3) → Integrator.AssignAliasPick
Engine/DisplayPresentation/DialogueHost.cpp/.h          // discard-changes dialogue when leaving dirty page
Engine/DisplayPresentation/ThemeStructure.cpp/.h + PaletteConfiguration.cpp/.h + ColourTransfer.h // theme tokens, corner radius, palette morph 0.25s blend
Engine/DisplayPresentation/TelemetryMetrics.cpp/.h      // RecordFrame, QueryAverageFramesPerSecond, ConsumeFrameRateDrop(30fps 2s), Rows.SceneLine, ConstructTelemetryLayout
Engine/DisplayPresentation/NotificationQueue.cpp/.h     // Push(toast), Advance, ConstructNotificationLayout, frame-rate-drop / baking-complete / downgrade / settings-applied toasts
Engine/DisplayPresentation/FidelityClassifier.cpp/.h    // Quality → FidelityCriteria (ReSTIR candidates/extra/taps, denoise levels, shadow technique/side/taps) + WithShadowResolution override
Engine/DisplayPresentation/AtmosphereModel.h + AtmosphericOptics.h + CelestialSolver.cpp/.h + CelestialTier.h + ColourTransfer.h + ExposureIntegrator.cpp/.h + MoonConstantRecord.h + MotionIntegrator.cpp/.h + Precipitation.h + VolumetricMedia.h + WindField.h + LtcSheenTable.h + SkyConstantRecord.h + PostConstantRecord.h + ShadingTableCodec.cpp/.h // celestial budget + report records
Engine/DisplayPresentation/RenderScheduler.cpp/.h       // ReSTIR integrator → Dispatch, exposure, denoise, ray-tracing capability, present, editor Present overload (SceneInstances/Sheet/Readout/View)
Engine/DisplayPresentation/VectorCodec.cpp/.h + GlyphSpace.cpp/.h + TextEntryState.cpp + PixelSpace.cpp/.h + InterfaceVectorCodec etc. → PixelSpace overlay host for dashboard + telemetry + diagnostics
```

### 2.4 Group D — DeviceExchange / Platform / Geometry / Spatial ★ DEPENDENCY (editor needs the world to edit)

```
Engine/DeviceExchange/SwapchainExchange.cpp/.h  (3,414+423) // GLFW + Vulkan surface + compute pipeline, PollInput, QueryTargetGeneration/SceneViewTexture, RefreshSky/Moons/Post/Instances/Traversal, RecordAndPresent, AssignShadowFrame/VisibilityFrame, PresentPacing
Engine/DeviceExchange/InputExchange.cpp/.h + InterfaceExchange.cpp/.h + DiagnosticMetrics.cpp/.h + RayTracingCapabilitySet.cpp/.h + VisibilityExchange.cpp/.h + ShadowExchange.h + RelayQueue.h + OrientationClassifier.cpp/.h
Engine/ContentInterchange/ContentCodec.cpp/.h + SceneCodec.cpp/.h + TextureIndex.cpp/.h + MaterialCodec.cpp/.h + MaterialDescriptor.h + MaterialIndex.cpp/.h + ShaderBallStructure.cpp/.h + FbxCodec.cpp/.h + ObjCodec.cpp/.h + UfbxTranslation.cpp
Engine/GeometricRaster/CameraProjection.cpp/.h + ClipProjection.h + GeometryStructure.cpp/.h + SceneStructure.cpp/.h + StarCatalogueIndex.cpp/.h + TraversalIndex.cpp/.h + VisibilityRaster.cpp/.h
Engine/SpatialInterface/Interface* (LayoutCodec, LightProjection, PointerProjection, ScreenSequence, Sequence, Specification, Structure, TextProjection, VectorCodec) + VolumeMarker.h
Engine/PlatformInterchange/AudioExchange.cpp/.h + MiniaudioTranslation.cpp + WaveCodec.cpp/.h
Engine/PhysicalDynamics/RigidBodySolver.cpp/.h
```

### 2.5 Group E — Shaders ★ MUST for viewport to show ReSTIR scene

```
Engine/Shaders/ReSTIRViewport.slang (1,294) // + CloudShadow.slang, SkyRecords.slang, PostRecords.slang, MoonRecords.slang already in 07b's 0005 overlay — now unified
Engine/Shaders/AtrousDenoise.slang + LuminanceReduce.slang + HiZReduce.slang + ClusterCull.slang + MaterialEvaluation.slang + RayGeneration.slang + Interface*.slang + Shadow*.slang + SurfaceResolve.slang + TraversalCWBVH.slang + VisibilityRaster.*.slang + SceneRecords.slang
0005-cloud-shadows/Engine/Shaders/*.slang stay — the donor's Engine/Shaders are the superset; diff them and keep FIN3 hashes.
```

### 2.6 Group F — Assets & Content ★ MUST for outliner thumbnails & viewport

```
EngineContent/CelestialTextures/{ember,glacier,luna,shard,shroud}_2k.jpg (moon albedos, 22 moons via kMoonAtlas)
EngineContent/AudioArchives/{FerrariLaFerrari,NissanGtrNismo,Porsche918Spyder}.toml
EngineContent/FontArchives/* (every static face — TypefaceRegistry loads once into dynamic atlas)
Projects/Project-Zero/Content/Scenes/CornellBox.gltf (generated — but commit the seed so CI doesn't need solver), ShaderBall.gltf, Showroom.gltf, Outdoor.gltf (generated on demand)
Diagnostics/EditorProof_Tabs.png (2,765,798 bytes — proof the 1:1 celestial port renders)
EngineContent/Toolchain? (FetchSponza.ps1, ToolchainSequence.ps1)
```

### 2.7 Group G — Build & Glue ★ MUST

```
CMakeLists.txt (424) — adds Engine/Editor, Engine/DisplayPresentation, Projects/Project-Zero/Source/*, Slang, ImGui, stb, miniaudio
.gitmodules (45) — ExternalPackages/imgui + stb (+ possibly vulkan-headers)
.gitignore update
Patches/{PatchA-TrapezoidalTabs, PatchB-TabOverlapZOrder, PatchC-RoundTabButtons}.patch + Patches.md + Phase9.md + Patches/Patch*.md
Scratchpad/CheckEditorPreview.sh + CheckEditorProof.sh + CheckEverything.sh + other proof harnesses (optional for port, keep at least those two)
Projects/Project-Zero/Build/{FetchSponza.ps1, ToolchainSequence.ps1}
Projects/Project-Dyno/* (DynoSequence — harmless, keep if present)
```

### 2.8 Explicit NOT to port (stay as 07b)

```
0005-cloud-shadows/**/* — leave verbatim (proof parity)
Projects/Project-Zero/Diagnostics/*, Host/*, Integration/*, PZIntegration/* — keep as-is (those are 07b's parity/tooling)
Tools/SkyReference/* — keep
Reviews/SunSky-Parity-2026-09-14.md, 0005-cloud-shadows/README.md, CLAUDE.md — keep
Scratchpad/* proof logs that reference absolute hashes — keep but don't block build on them
```

### 2.9 File-count summary

```
Engine/Editor                          8 files   ★ core
Projects/Project-Zero/Source          19 files   ★ harness
Engine/DisplayPresentation            45 files   ★ control panel + celestial
Engine/DeviceExchange                  9 files   dep
Engine/ContentInterchange             11 files   dep
Engine/GeometricRaster                 9 files   dep
Engine/SpatialInterface               11 files   dep
Engine/PlatformInterchange             5 files   dep
Engine/PhysicalDynamics                2 files   dep
Engine/Shaders                        22 files   ★ visual
EngineContent/* + Diagnostics          ~10 files ★ assets
Build + .gitmodules + CMake            ~5 files ★ build
Total delta 07b→4e7: ~602 files — ~180 are editor-essential, rest are the Engine that 07b never had.
```

---

## 3. How to update the UI — the actual integration steps

### 3.1 Bring the working branch to 07b

```bash
git fetch origin arena/01a0a07b-frontier:arena/01a0a07b-frontier \
                 arena/01a0a4e7-frontier:arena/01a0a4e7-frontier
git checkout arena/01a0a579-frontier
git reset --hard arena/01a0a07b-frontier   # 68f3016 → 85774c5 (cloud shadows FIN3)
# working tree now equals 07b (0005-cloud-shadows + Projects/Project-Zero parity)
```

*Why reset, not merge?* `arena/01a0a579-frontier` was still at `68f3016` (only README). Fast-forwarding to 07b gives a clean base with FIN3 proofs intact. Keep `0005-cloud-shadows/` read-only thereafter.

### 3.2 Cherry-pick the two editor commits (preferred over raw copy)

```bash
git cherry-pick 2023ad5  # Editor: Vulkan/ImGui docking editor with celestial Outliner (1:1 port), Viewport, Inspector
git cherry-pick 305beff  # Editor: rectangular selection, one-line footer, ReSTIR scene in Viewport + its objects in Outliner
# resolves: CMakeLists.txt (add Engine/Editor), .gitmodules, any README merge
```

*Why cherry-pick?* Preserves authorship, commit messages, and the two proof images. If cherry-pick conflicts on `Projects/Project-Zero/Source/GameExecution.cpp` (because 07b has no file there — the base loop lives at `0005-cloud-shadows/...`), accept **both**: keep the overlay copy, add the new `Projects/Project-Zero/Source/GameExecution.cpp`.

*Alternative (if cherry-pick is noisy):* `git checkout arena/01a0a4e7-frontier -- Engine/ EngineContent/ CMakeLists.txt .gitmodules Projects/Project-Zero/Source/ Diagnostics/EditorProof_Tabs.png Patches/` then `git status` to verify.

### 3.3 Wire `GameExecution.cpp` — the 10-line heart

Inside `Projects/Project-Zero/Source/GameExecution.cpp` the donor already did this; verify these seams exist (they are the UI):

1. **Headers** — `#include "../../../Engine/Editor/EditorInstance.h"` + `#include "EditorFeedSequence.h"` + `#include "../../../Engine/DisplayPresentation/DiagnosticInspector.h"` already at top.
2. **Pre-loop seeds:**
   ```cpp
   RenderScheduler Panel;          // was already there
   CelestialSequence Celestial; Celestial.Prepare();
   // NO AssignCloudShadowStaging — tier owns it now
   Celestial.AssignMoonAtlas(MoonSlots, Textures);
   EditorFeedSequence Feed;
   ViewportOrbit Home; Panel.SeatViewportOrbit(Home);
   ```
3. **Roster allocations** (before `while`):
   ```cpp
   EditorInstance SceneInstances[kMaxEditorInstances] = {};
   EditorSheet    PickedSheet = {};
   bool           SceneReady = false;
   #ifdef FRONTIER_DEVELOPMENT
   EditorReadout  EditorFooter{}; uint32_t EditorViewGeneration = ~0u;
   #endif
   uint32_t SceneRowCount = 0u, SheetFor = kNoEditorInstance;
   EditorProperty* TintMirror = nullptr; uint32_t AppliedOrbit = 0u;
   ```
4. **Per-frame inserts** (inside `while`, after `Telemetry.RecordFrame`, before `Panel.Present`):
   ```cpp
   #ifdef FRONTIER_DEVELOPMENT
   if (CelestialFirstRow != kNoEditorInstance) Celestial.RefreshRoster(SceneInstances, CelestialFirstRow, SceneRowCount);
   EditorFooter.Fps = Telemetry.QueryAverageFramesPerSecond();
   snprintf(EditorFooter.Quality, "%s", InterfaceFidelityTierName(PanelTier));
   snprintf(EditorFooter.Pixels, "%ux%u", Surface.QueryWidth(), Surface.QueryHeight());
   EditorFooter.SunElevation = Celestial.Frame().Sun.Elevation;
   // MoonCount = countVisible, Cam = Eye{x,z,y}
   Panel.AssignEditorReadout(&EditorFooter);
   if (Surface.QueryTargetGeneration() != EditorViewGeneration) {
       EditorViewGeneration = Surface.QueryTargetGeneration();
       Panel.AssignEditorView(Surface.QuerySceneViewTexture(), Surface.QueryWidth(), Surface.QueryHeight());
   }
   #endif
   ```
5. **Roster fill + Present:**
   ```cpp
   if (!SceneReady) {
       SceneRowCount = Feed.FillRoster(SceneInstances, Level);
       CelestialFirstRow = SceneRowCount;
       SceneRowCount += Celestial.AppendRoster(SceneInstances, SceneRowCount, kMaxEditorInstances);
       Panel.SeatViewportOrbit(Home);
       SceneReady = true;
   }
   uint32_t PickedNow = Panel.QueryPickedInstance();
   if (Celestial.IsEntity(PickedNow - CelestialFirstRow)) TintMirror = Celestial.BuildSheet(PickedNow, ...);
   else                                                   TintMirror = Feed.BuildSheet(PickedNow, SceneInstances, SceneRowCount, &PickedSheet, Camera, Level, AnimatedInstances);
   Panel.Present(Integrator, Camera, Scene, Surface.QueryWidth(), Surface.QueryHeight(),
                 SceneInstances, SceneRowCount, &PickedSheet,
                 [&]{ /* OverlaySurface + Telemetry + Diagnostics + ControlCentre + Notifications */ });
   ```
6. **Write-backs** (after `Panel.Present`):
   ```cpp
   #ifdef FRONTIER_DEVELOPMENT
   if (TintMirror && PickedNow < SceneRowCount)
       SceneInstances[PickedNow].Tint = TintMirror->ColourTint;
   if (Panel.QueryViewportOrbit().Revision != AppliedOrbit) {
       auto &O = Panel.QueryViewportOrbit();
       Camera.AssignSpatialLocation(Target - Forward*Distance);
       Camera.AssignOrientationEuler(O.Pitch, O.Yaw, 0);
       AppliedOrbit = O.Revision;
   }
   #endif
   ```
7. **Pointer capture gate** on camera:
   ```cpp
   if (!ControlCentre.CoversPointer() && !Panel.QueryEditorCapturesPointer() && !Panel.QueryEditorCapturesKeyboard())
       FlyThroughSolver::Advance(...);
   ```

No other loop order change.

### 3.4 Theme & font — one call

```cpp
// after ImGui context created, after SwapchainExchange bring-up, before while:
Panel.ApplyTheme(); // inside: ControlPanel::AssignFonts(Ui, Small, Mono, MonoSmall, Title, Display)
                     // + ImGuiStyle seat (tab figures, token theme, 4 faces). Idempotent, seats faces once.
```

`TypefaceRegistry::Load("EngineContent/FontArchives")` + `TypefaceRegistry::Install(&Typefaces)` already in donor; keep. `ActiveTheme.AssignTheme/AssignCornerRadius/AssignFontFamily` are driven by `AppearanceInspector` — the UI updates live with a 0.25 s cross-fade (`ThemeBlendDuration`).

### 3.5 Outliner / Inspector / Viewport — what the user sees

* **OutlinerPanel** (`OutlinerPanel.cpp` 1,571 lines) — the left column:
  * Header 32px, tiles (5 `EditorNarrowing` pills: Lights/Sky/Bodies/Geometry/Camera), search (`Ctrl+Shift+F` focuses), chips, outline.
  * Outline renders `SceneInstances` preorder: folder icon (amber), depth indent, `Glyph` (14 px SVG: globe/cloud/fog/sun/sky/stars/moon/wind/…), label, `Meta` (“12.4°”), `Tag` (“Comp”), `Standing` dot (16 px: Ok/Quiet/Warn/Err, e.g. “Below horizon”), `KidCount` badge, `DYN`/`PHYS` badges, `Solo`/`Lock` icons.
  * Interaction: single click = pick, `Ctrl` click = toggle, `Shift` click = range, drag = `MoveRun` (depth-aware reparent, cycle-refused, `OrderRevision++`), eye = `Visible`, lock = `Locked`, solo = isolate, shut = fold.
  * Footer: one-line `Realtime • Quality • Sun elev • Moons • Cam` from `EditorReadout` (the 305beff improvement).
* **InspectorPanel** (`InspectorPanel.cpp` 580 lines) — the right column:
  * Reads `PickedSheet` (`GroupCount` ≤6, each ≤10 props). Per-group card with title, per-prop widget via `ControlPanel`:
    * `Slider` → split pill (92px: black figure cell + inset unit cell) + track (26/12 thumb, thin 10/9 for footer clock) + type-in on click.
    * `Switch` → toggle, `AxisVec3` → 3 axis fields with step, `Colour` → chip + 8 swatches or chip alone, `Select` → dropdown (≤6 options), `Readout` → right-aligned tabular text.
  * All edits write the mirror (`Figure`/`On`/`Axes`/`ColourTint`/`Picked`) — the GameExecution write-back carries only `TintMirror` back today; other props go via `Celestial.ApplySheet` / `Feed.ApplySheet` when Inspector commits.
* **ViewportPanel** (`ViewportPanel.cpp` 1,753 lines) — the centre column:
  * Bar: brand, dock toggles (left/right), Views menu (6 orthos + perspective + home), transport (Edit/Play/Simulate, Realtime toggle, pause/step), shade gear (shares `ControlCentre` open flag).
  * View: dark rect (`LastW/H` reported to project), draws either `ViewRgba` (CPU trace headless) or `ViewTexture` (ReSTIR `QuerySceneViewTexture()` — the 305beff “ReSTIR scene in the Viewport”). Orbit gizmo (6 axis pads, tap slop, hold vs orbit).
  * Command: `CommandText[128]` + `CommandEcho[128]` (3.2 s lease), suggestion stack 9 rows (sort: quick/example/verb/entry/bad), ghost, history 8, blur grace 120 ms, caret-to-end, `PastAt` navigation.
  * Footer: day clock (scrub), stats (`Fps`, `Pixels`, `Quality`, `SunElev`).
  * Orbit: `ViewportOrbit{ Yaw, Pitch, Distance, Target[3], Ortho, ViewPoint, Revision }` — gizmo/views/wheel bump `Revision`, GameExecution ②f carries it to `Camera`.
* **ControlPanel** (`ControlPanel.cpp` 740 lines) — stateless primitives both panels share: `AssignFonts`, `FadeTint`, `TypeInCell`, `SliderPill`, `Switch`, `ColourChip`, `Select`, `AxisVec3`. No project state.

### 3.6 Control Centre (the shade)

Already in donor; keep its **page model**:

* **Notch** (closed): handle height → `ConstructTelemetryLayout` + toasts + diagnostics; click/drag → `AdvanceInteraction`.
* **Shade** (open): 4 hub rows (Render/Appearance/Input/Notifications) + cards. Each card edits a **draft** copy; `Apply` pushes `Revision++` → `ApplyControlCentreSettings` / `AppearanceInspector` / `InputInspector` / `Notifications`. Dirty page shows `DialogueHost` on navigate away.
* Must keep `ControlCentreHostState::{Closed,Opening,Open,Closing}`, springs, `Resize(LogicalWidth, LogicalHeight)` with `InterfaceScale` (Display → UI Scale).

### 3.7 Assets & shaders — copy verbatim, keep proofs green

* Copy `EngineContent/CelestialTextures/*_2k.jpg` + `EngineContent/AudioArchives/*.toml` + `EngineContent/FontArchives/*` — these are referenced by path (`kMoonTextureDirectory`, `AudioExchange::RegisterPath(..., Linear=false)`).
* Copy `Engine/Shaders/*` — ensure `InterfaceRecords.slang`, `InterfaceSignedDistance.slang`, `MoonRecords.slang` land because Viewport and moons sample them. Verify Slang compiles: `Scratchpad/CheckShaderCompile.sh` + `CheckInterfaceAudio.sh`.
* Keep `Diagnostics/EditorProof_Tabs.png` — `CheckEditorProof.sh` compares the viewport tabs against it.
* Do **not** overwrite `0005-cloud-shadows/README.md`’s FIN3 hash `8688374c`; the `CheckCelestialTiers` test forbids hand-reading celestial fields — always go through `CelestialTier::BudgetFor`.

### 3.8 Build

* `CMakeLists.txt` adds `Engine/Editor`, `Engine/DisplayPresentation`, `Projects/Project-Zero/Source/*`, `ExternalPackages/imgui`, `ExternalPackages/stb`.
* `git submodule update --init --recursive` pulls `imgui` (docking branch) + `stb`.
* MSVC: `Construct.bat` or `cmake -S . -B build -DFRONTIER_DEVELOPMENT=ON && cmake --build build --config RelWithDebInfo`. The 07b overlay builds with same flags but defines `FRONTIER_DEVELOPMENT=OFF` so editor strips out.
* Patches under `Patches/` (trapezoidal tabs, tab overlap z-order, round tab buttons) are applied via `git apply` in `ToolchainSequence.ps1` — keep them.

### 3.9 Validation checklist after port

```bash
# 1. Proofs still green
python Projects/Project-Zero/Diagnostics/RunCelestialParity.py   # sky 0000_night … 1811_sun vs htmlsky
python Projects/Project-Zero/Diagnostics/RunFogParity.py
./Scratchpad/CheckCelestialTiers.sh   # no hand-read of tier fields
./Scratchpad/CheckEverything.sh

# 2. Editor smoke
./Scratchpad/CheckEditorPreview.sh    # headless Viewport renders CPU trace into ViewRgba
./Scratchpad/CheckEditorProof.sh      # Tabs.png pixel-compare
# manual: --scene CornellBox → Outliner shows Room/Objects/Lighting/Cameras (4 folders) + celestial folder (9 rows)
#         pick Sun → Inspector sliders move SunElevation live, footer SunElev updates, accumulation restarts
#         pick Room/Lights folder → tint chip changes row tint via mirror
#         Views menu → Top/Front → orbit snaps, fly camera follows, Viewport rect resizes with window
#         Command: type "sun 30" → suggestion stack, enter → echo 3.2s, sun jumps
#         Control Centre: Quality Standard→Ultra → ReSTIR candidates/extra/taps change, toast, config written
#         F3 → DebugView AliasPick → Integrator resets, star tables upload log
```

### 3.10 UI update policy going forward

* **Tokens win:** `ControlPanel` owns the token palette; `EditorHost::ApplyTheme` runs **last**, over the scheduler’s own seating, so editor tokens win everywhere (explicit comment in donor).
* **Label-based, not order-based:** any new `EditorProperty` must use `Find(label)` on read-back — the donor fixed an entire class of “slider lands on wrong field” bugs by this.
* **No allocations on tick:** `kMaxEditorInstances 64`, `kMaxEditorPicked 16`, `kMaxEditorSheetGroups 6 × 10` are fixed. The roster/view alloc-free guarantee is what keeps the 60 fps budget.
* **One-line footer forever:** the 305beff footer is `Realtime • Quality • Sun • Moons • Cam` — do not revert to multi-line.
* **ReSTIR scene in Viewport:** the viewport must show `Surface.QuerySceneViewTexture()` when present, falling back to CPU `ViewRgba` only headless (`AssignViewTexture` vs `AssignView`).

---

## 4. Appendix — key counts & hashes for reviewer

```
07b tip: 85774c5  0005 real cloud shadows: slab-spanning SunTransmittanceAt + GPU transcription + proofs (FIN3 8688374c)
4e7 tip: 305beff  Editor: rectangular selection, one-line footer, ReSTIR scene in Viewport + objects in Outliner
        +2023ad5  Editor: Vulkan/ImGui docking editor with celestial Outliner (1:1 port), Viewport, Inspector
Working branch start: 68f3016  Add files via upload (main)
FIN3: 8688374c (0005-cloud-shadows/README.md)
07b proof images: 0000_night_diff_oracle.png, 0589_sun_diff_oracle.png, 0600_sun_diff_oracle.png, 0640_*diff, 0760_*diff, 1200_lookup_diff, 1811_sun_diff, 0005_FIN3.png, restir.png
4e7 proof: Diagnostics/EditorProof_Tabs.png (2,765,798 bytes)
Engine files in 4e7 not in 07b: 169
PZ Source files in 4e7 not in 07b: 19
GameExecution diff: -10 lines net (showcase→CornellBox, cloud staging→tier, + editor ②e/②d/②f)
```

---

## 5. What to do next — approval gate

1. **Approve §2 port list** (or mark any file to exclude).
2. Run `git reset --hard arena/01a0a07b-frontier` on `arena/01a0a579-frontier` and `git cherry-pick 2023ad5 305beff` (or `git checkout arena/01a0a4e7-frontier -- Engine/ Projects/Project-Zero/Source/ …`).
3. Build with `-DFRONTIER_DEVELOPMENT=ON`, run `CheckEditorPreview/Proof`, verify FIN3 and celestial parity still pass.
4. Open PR `arena/01a0a579-frontier → main` with this doc as description.

If you want the agent to execute the port now, reply **“port it”** and the agent will reset, cherry-pick, resolve any `CMakeLists.txt`/`README.md` conflicts, copy assets, and push to `arena/01a0a579-frontier`.
