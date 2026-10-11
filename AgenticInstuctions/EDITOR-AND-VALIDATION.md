# The Editor vs The Validation Shell — READ BEFORE TOUCHING EITHER

> This note exists because a previous agent ported the validation shell into the
> editor host (recorded `GlobalShellPanel` fullscreen over the editor window) and
> drew the sky in the shell's own viewport. That was wrong on both counts and was
> reverted. It must not happen again.

## The two surfaces are different products

```
VALIDATION HOST (InterfaceValidationHost)                 EDITOR HOST (EditorHost)
────────────────────────────────────────                  ────────────────────────
prototype of the WHOLE reference sheet                    the real editor layout
                                                          ┌─ Workspace 1 ──────────────┐
┌──────────────────────────────────────┐                  │ ┌───────────┬─────────────┐ │
│ top bar                              │                  │ │ VIEWPORT  │ OUTLINER    │ │
├──────┬───────────────────────────────┤                  │ │ leaf      │ leaf        │ │
│OPTION│ GlobalShellPanel viewport     │                  │ │ (sky)     │ (directory) │ │
│ S    │  ┌─────────────────────────┐  │                  │ │           │ ┌───┬─────┐ │ │
│ rail │  │ BLACK viewport (stays   │  │                  │ ├───────────┤ │out│det. │ │ │
│ CAD… │  │ black — keep it black)  │  │                  │ │ footer    │ ├───┴─────┤ │ │
├──────┴──┴─────────────────────────┴──┤                  │ │           │ │ props   │ │ │
│ fullscreen inspector strip:           │                  │ │           │ │ history │ │ │
│ [outliner|details]→[properties|hist]  │                  │ └───────────┴─┴─────────┴─┘ │
│ + texture-paint layer stack           │                  │   split / + / resize /      │
└──────────────────────────────────────┘                  │   withdraw via EditorPanel  │
                                                          └──────────────────────────────┘
```

## Rules

1. **`GlobalShellPanel` is recorded ONLY by `InterfaceValidationHost`.** It is a
   prototype of the full reference sheet (options rail, texture-paint layers,
   CAD drafting, fullscreen two-slide inspector). Its viewport stays **black**
   (that is the prototype's own look) — do not draw a sky in it.
2. **The editor host NEVER records `GlobalShellPanel`.** The editor's layout is:
   - `WorkspacePanel` + `WorkspaceIndex` + the vendor dock → workspace windows
   - `EditorPanel` + `PanelStructure` → splittable panels (viewport | UV |
     outliner | properties), each with its own chrome (header, footer, subject
     menu, divide menu) — this is how panels are added, split, resized, withdrawn
   - `SceneDirectoryPanel` → the CONTENT inside the leaves: the sky in a
     viewport leaf, the outliner | details column in an outliner leaf, the
     properties | history pages in a properties leaf
3. **Drafting and the game editor are the same thing** — one general-purpose
   outliner. The editor's outliner leaf presents the scene directory; there is
   no mode switcher and no texture-paint in the editor.
4. **The sky belongs in the editor's viewport LEAF** (the GPU sky texture via
   `SceneDirectoryPanel::RecordViewportSky`), never in a fullscreen overlay.
5. Shared scene-directory contracts (entities, environment, revisions) live in
   `SceneDirectoryPanel/Api/SceneDirectoryContract.h` — both surfaces include
   that one header; neither one owns the other.
6. History records once per slider drag (start → end) via `RevisionDemandSlot`
   — the host drains it exactly once per drag. Do not regress this to per-tick.
7. Linux/Windows parity: the sandbox (`ConstructSandbox.py`) is the POSIX twin
   of `Build/Construct.ps1`; `.toml` files and `.slang` shaders must stay in
   sync with renamed C++ definitions.
8. Proofs: real pixel renders under `VisualProof/EditorScene/` — never ASCII or
   stubs.

## Where things live

| Concern | Component |
|---|---|
| Workspace windows, tabs, dock | `WorkspacePanel`, `WorkspaceIndex` |
| Split/resize/withdraw chrome | `EditorPanel`, `PanelStructure`, `LeafPanel` |
| Editor leaf content (sky, outliner, properties) | `SceneDirectoryPanel` |
| Sky GPU texture (upload, device rebuild) | `AtmospherePresentationSurface` |
| Sky evaluation (dome + sun disc) | `GenerateSkyImage` (`Application/EditorHost`) |
| Camera pose and movement | Base `CameraComponent` (`Application/CameraComponent`), editor identity `EditorCameraComponent`; input via `InterfaceExchange::CameraInput` |
| Validation prototype shell | `GlobalShellPanel` — validation host only |

## The editor camera

- **Registered in the scene directory** as the "Editor Camera" row (last row, so
  the Sun/Sky ordinals and their history ordinals never move). Its details pane
  shows the pose (position, yaw/pitch, speed) and its toggles; its properties
  leaf has a live **Fly Speed** slider (1–500 m/s) with a once-per-drag history
  demand like the environment sliders.
- **Movement is Unreal-fly**: W/S along the view direction (pitch included),
  A/D strafe, E/Q world up/down, **Shift to boost the fly speed 3x**; **hold
  the right mouse button and drag to look**. The look gesture CAPTURES the
  cursor: while held, the OS cursor is warped to the display centre every tick,
  so the turn is unbounded — it never stops at the window edge.
  🔴 The look reads the SEAM'S OWN CURSOR TRACKING, never `io.MouseDelta` and
  never the pointer's departure from the centre. It is NOT gated on
  `WantCaptureKeyboard` (a hovered window raises it, which would stop the
  camera over any panel). Only text input gates the movement keys.
  🔴 WARP TIMING LESSON (do not regress — this has broken the camera three
  times): the look gesture must NOT read `io.MouseDelta`, because EVERY cursor
  warp corrupts it:
    - through the backend's `WantSetMousePos`, the warp fires
      `glfwSetCursorPos` at the start of the NEXT `ImGui_ImplGlfw_NewFrame`,
      clobbering the just-polled motion — delta reads zero;
    - from `Seal()` after `ImGui::Render()`, `MousePosPrev` is captured BEFORE
      the warp, so the next delta measures from the pre-warp position, not the
      centre — the camera turns once, then stops.
  The correct pattern is in `InterfaceExchange::CameraInput`: read
  `glfwGetCursorPos` directly, measure against the seam's own previous sample
  (`LookLastX/LookLastY`), then warp to the window centre mid-frame (the
  callback QUEUES the centre event, processed at the next NewFrame after the
  next poll's motion — panels keep the true pointer, the camera turns
  continuously, unbounded).
- **Camera lag**: the rig eases position and yaw/pitch toward the target with
  an exponential time constant (0.18 s). Toggle it in the camera's details
  (Camera Lag); Invert Pitch is the second toggle. The viewport crop reads the
  LAGGED pose, so a fast turn visibly trails the input.
- **The world is visible**: the viewport leaf draws a ground lattice on the
  Y=0 plane, projected through the same pinhole as the sky — so W/A/D/E/Q
  visibly travel the scene and the look gesture visibly turns it, in metres.
  The lattice is driven by the viewport's footer "Grid settings" popup: None /
  Lines / Dotted / Lines + Dots, cell Scale (m), Subdivisions (the half-extent
  in cells, 2-128), and the red/green/blue axis lines (X/Y/Z toggles). The
  dotted presentation draws a node at every intersection.

## The editor's texture-paint layer stack

The editor's layer stack is a DEDICATED class, `TexturePaintPanel`
(`SlateUI/Interface/TexturePaintPanel`), a sibling of SceneDirectoryPanel — NOT
the validation host's `LayerStackPanel` (that one stays the prototype's, with
its history spine; the editor's has none, exactly as the user asked).

- **The stack page IS the LayerstackV1 reference** — appearance and gestures
  faithful to the HTML, not a simplified row list:
  - HEADER: "LAYERS" + the "N · Mm" count chip + the SOLO chip (while a row is
    solo'd) + undo/redo (drawn disabled — no history spine) + the expand
    toggle (the reference's wide columns) + the solid Add button (rightmost).
  - TOOLS: the search pill ("Filter layers…"), the separator, and the folder /
    mask / collapse-all tools. The same FacetPanel filter card (layer
    categories) sits below, as the user required.
  - ROWS (45 px): the 3 px colour tag (dotted on a mask), the disclosure
    chevron, the eye, the 35 px square thumb (checker + folder glyph for
    folders; hue wash + the type badge for layers), name + the sub run
    (Type · blend · op%, or "N items · blend" for a folder), the chips
    (3D / L / MASK / n FX / x/8 CH), the details chevron and the "more" menu.
  - MASKS: the attached 37 px row with the connector elbow, the dashed border,
    the uppercase MASK name, "source · Gray 8 · density · INV", chips, the
    details chevron and its menu.
  - FOLDERS: children indented with the colour guide line.
  - FOOTER: the crumb, the blend pill (its menu carries the 13-blend roster
    with the check on the standing blend), the opacity slider + value pill,
    and the action bar (paint / fill / adjustment / filter / decal / pattern ·
    group / duplicate / lock · move up / move down · delete). The opacity
    slider drives the layer's opacity, or the MASK's density while a mask is
    taken, exactly as the reference's footer does.
- **Menus are the reference's `.pop`**: a rounded card with pill options,
  recorded ABOVE the whole page inside the leaf, opened from the Add button
  (Paint/Fill/Adjustment/Filter/Decal/Pattern/Group), the row's more button
  (Details / mask / lock / solo / duplicate / group / the ten colour swatches /
  delete), the mask row's more button (Details / Invert / Delete mask) and the
  blend pill. One menu stands at a time through the ledger's disclosure slot;
  a contact outside the card withdraws it.
- **The flow**: the leaf is a two-page carousel. The STACK page is the HTML's;
  Tab (or a row's details chevron) TOGGLES to the PROPERTIES page, landing on
  the tab the selection names:
  - a layer row → Channel Properties (per-channel dot / name / blend / opacity,
    with its own search pill + channel-group facets: Base, Maps, Output)
  - a mask row → the Mask panel (source dropdown, density, invert, applies-to
    chips)
  - a decal / pattern / generator → its settings card
  - a FOLDER → the COMBINED stack properties (layer count, mask count, channel
    union, passthrough, opacity — one summary over the whole subtree)
  The property tabs are switched with the strip; Tab never cycles them.
- **The per-row working copies** (opacity, blend, lock, mask, tag hue) are
  context-owned and seeded from the rows by `SeedPaintContextFromRows`; the
  structural operations (add / duplicate / group / move / delete) are one
  request slot per tick (`TexturePaintRequest`), drained by the host through
  the shared `TexturePaintStack` helper (`Seed` + `ApplyRequest`), which the
  harness drives identically so the two can never drift.
- **The editor's contract** is `TexturePaintContract.h` (`TextureLayerRow`,
  `TextureLayerClassification`, tags, sources, detail runs, the blend and mask
  rosters, `TexturePaintStack`). Do not merge it with the validation
  `LayerStackPanel`'s types.
- **The editor host** owns `TexturePaintPanel` + `TexturePaintContext` + the
  mutable `TexturePaintStack`, seeds the reference's mock tree, feeds the
  search pill gated on `SearchTaken`, and routes Tab to the texture-paint panel
  only when the pointer is over one of its leaves (the scene directory keeps
  Tab otherwise). `PanelSubject::TexturePaint` is the fifth panel type
  (appended, so ordinals 0-4 never move); the vacant chooser and the subject
  menu offer it, and `LeafPanel::LayerStack` is its chrome ground.
- **The proof** (`editor-layerstack`): stack rows render with their colour
  tags (exact hue pixels), the attached mask row's dashed border stands, the
  footer's blend/opacity/action bar draws, "decal" narrows the search to one
  row, the Fill facet narrows to the fills, a layer + Tab opens Channel
  Properties, a mask + Tab opens the mask panel, the details chevron travels,
  the more menu opens and its Duplicate inserts a row (13 rows stand) and the
  trash removes it again, the SOLO chip's yellow pixels stand in the header,
  the blend menu takes Screen, the opacity drag writes 60 %, the lock button
  locks, the expand toggle stands and retires the wide columns, and a folder +
  Tab opens the combined stack page — all asserted on real pixels.

## The GPU overlay pass (grid, gizmo, wireframe)

The grid, the world-origin gizmo and (later) wireframe are NOT drawn through
the interface's ImGui draw lists — they are drawn by `OverlayPass`
(`SlateVulkan/Device/OverlayPass`), a dedicated graphics program recorded in
its own pass inside the host's dynamic-rendering scope, AFTER the interface.

- **The CPU never tessellates.** The panel fills `OverlayGeometry`
  (`Shared/OverlayGeometry.slang.h`, reachable from every unit) — a few hundred
  screen-space primitives with two points per line and one per dot. The host
  uploads it when its generation changes, and the vertex shader
  (`OverlayVertex.slang`) expands lines and dots from `SV_VertexID` into their
  quads; triangles pass through. ImGui's polyline path tessellated every
  segment on the CPU, which bogged the frame down on dense lattices and would
  on high-poly wireframe.
- **Vivid colours.** The pass blends STRAIGHT alpha (`src_alpha /
  one_minus_src_alpha`) with no tone mapping, so a full-opacity gizmo stays
  full-opacity — the interface's premultiplied blend washed the same hues out
  over a bright sky.
- **Lifecycle**: `Overlay.Reclaim()` on device-retiring, re-Construct on
  device-recovered, `Overlay.Reclaim()` before `Lifetime.Reclaim()` at
  shutdown. The pass refuses gracefully when the build lowered no shaders (the
  sandbox) and the editor runs without the overlay.
- 🔴 NDC CONVENTION LESSON (do not regress — this hid the grid three times): the
  overlay pass draws AFTER the interface, so its vertex transform must match the
  interface's own vertex shader to the pixel: screen y grows DOWNWARD and NDC
  +1 is the framebuffer's TOP row, i.e. `NDC.y = 1 − 2y/h`. The transform
  lives in `Shared/OverlayTransform.slang.h` (`OverlayNdcX` / `OverlayNdcY`),
  compiled by BOTH the shader toolchain and the harness, and the harness's
  `editor-grid-settings` asserts the convention on real numbers BEFORE any
  capture (y=0 must land on +1; mid 0; bottom −1). The previous spelling
  `−2y/h − 1` mapped every positive screen y BELOW NDC −1, clipping the whole
  overlay off-screen — the grid, the axes and the gizmo silently drew nothing,
  while the harness's CPU twin rasterized in screen space and never exercised
  the math, so the defect shipped repeatedly.
- **The overlay is clipped to the viewport leaf.** The pass's `Record` sets the
  scissor to the leaf's box (clamped to the display), and the host keeps ONE
  overlay record per viewport leaf (static storage) and draws each clipped to
  its own leaf — the grid, the axes and the gizmo never paint over the
  outliner, the properties or any other panel, and two viewports each show
  their own grid. The harness rasterizes with the same clip and asserts zero
  overlay ink beyond the leaf.
- **The axis lines span the whole grid**: X (red), Y (green, vertical) and Z
  (blue) each run the lattice's full extent in both directions (-Half ..
  +Half), sampled and near-plane-split like the lattice lines, and they draw
  even when the lattice presentation is None (the two toggles are
  independent).
- **Shaders**: `OverlayVertex.slang` + `OverlayFragment.slang` under
  `SlateVulkan/Device/OverlayPass/Shader/` are lowered by `Construct.ps1` to
  `<Binary>/../Shader/SlateVulkan/<Stem>.spv`; `ShaderCodec` reads them at the
  same directory the build writes.
- **Proof**: the harness rasterizes the SAME `OverlayGeometry` record on the
  CPU (its `RasterizeOverlay`) with the same straight-alpha blend, so the proof
  pixels are the pass's input; `editor-grid-settings` asserts all three axes
  (R/G/B at full opacity), the gizmo's white centre handle, and that
  lines+dots carries ~6x the lattice ink of dots-only.
- 🔴 HARNESS LESSON (do not regress): `SceneDriver` must own its
  `ThemeProfile` as a MEMBER — the panels borrow it and read it every tick.
  A stack local in `Construct` is use-after-scope the moment the struct grows:
  intermittent segfaults, flat dark-blue renders and "missing" axes/rows all
  came from exactly that, and ASAN caught it at `WorkspacePanel::Record`.
- 🔴 SHADOWING LESSON (do not regress): in `FacetPanel::Arrange` and
  `Record`, the local figures were named `ChipHeight` / `HeaderHeight`, the
  SAME names as the anonymous-namespace constants they scale — a local
  shadowing a global by the same name reads the global's uninitialised slot,
  collapsing the whole card (the "squashed filters": zero-height chips,
  zero-height header, clipped dropdown). The locals are `ChipRowY` /
  `HeaderRowY` now.
- The sky dome is direction-indexed and camera-independent: looking around only
  moves the crop, never regenerates the texture. The viewport samples the dome
  through a PERSPECTIVE mesh (per-vertex UVs along the true pinhole ray), so
  the sun stays round at any leaf aspect — a plain cropped quad stretches it.
  The mesh's U is ABSOLUTE and the sampler wraps U (REPEAT, V clamps): the
  dome's azimuth is periodic, and a camera whose frustum crosses the seam must
  wrap, not shift — a shift of yaw/2π is not a whole period and reads the
  wrong texels (this was tried and reverted).
- **Dropdowns composite above the viewport**: `EditorPanel::Record` can defer
  its popups (`DeferPopups`), the host records leaf content between the two
  calls, then `RecordDeferredPopups` records the menus on top. Never record the
  leaf content AFTER the deferred call.
- **The outliner leaf has pages**: Tab (the seam's Summon) cycles Directory →
  Properties → History → Directory; the leaf's bottom strip selects the same
  page, and the "Inspect" call in its header jumps to Properties. The
  properties leaf's own Properties | History strip is a separate tab state, so
  the two leaves never fight.
- **Search + filter in the directory**: a search field sits between the
  outliner's header and its rows (the reference's "Filter Entities…" box,
  which the editor lacked). It is GATED: the panel reports `SearchTaken` and
  the host feeds the seam's `AcceptTyped` only while the field holds the
  contact — the validation shell captured every keystroke unconditionally,
  which is the "search box not working" defect. Backspace deletes, Escape
  clears. Below the search box sits the validation UI's generic FacetPanel
  ("Filters"): wrapped category chips (Objects, Lights, Cameras, Folders,
  Audio, Particles, Triggers, Environment, Layers), individual removal,
  clear-all and a shared dropdown. Every `EntityRow` carries a `Tagged` run
  (space-separated, borrowed) — a row matches when its NAME or its TAGS
  contain the search run AND its category's facet is enabled; a row also
  shows when a descendant matches, and while the filter stands every branch
  is forced open. All facets off = no filtering; an empty run = no search.
  The Add-filter control is a compact PILL stuck to the card's left edge
  (132 px, never full-width), and the chips wrap to new rows with the gap
  between them — exactly the reference's flex-wrap. The harness's
  `editor-search-filter` asserts the typed run lands, the tag
  "fly" finds the camera (tagged "camera fly view") even though no name
  contains it, the Lights facet narrows to two rows, and "zzz" reaches the
  empty state.
- **Shutdown order matters**: `SkySurface.Reclaim()` runs BEFORE
  `Lifetime.Reclaim()` — a surface left standing past the device reclaim waits
  on a dead fence and reports `vkWaitForFences: Invalid device` at exit.
- 🔴 FALLBACK LESSON (do not regress): when the overlay pass could not stand (a
  build that lowered no shaders, or a device that refused it), the host draws
  the SAME `OverlayGeometry` through the interface —
  `SceneDirectoryPanel::RecordOverlayFallback` (lines via `Polyline`, dots via
  `Medallion`, triangles via `Tongue`, all confined to the leaf) — so the grid,
  the axes and the gizmo are ALWAYS visible. The editor must never silently
  lose its overlay; the CPU cost is paid only when the GPU pass is absent. The
  harness's `editor-overlay-fallback` proves the fallback pixels (axes hues,
  lattice ink, the gizmo's white handle, zero bleed outside the leaf).
  🔴 POPUP LESSON: the GPU overlay pass records AFTER the interface, so an open
  editor popup (grid settings, subject menu, ...) would be painted over by the
  grid and the axes — the reported "the lines draw on the ImGui menus". The
  host tests `EditorPanel::AnyPopupStanding()` and withholds every leaf overlay
  while a popup stands; the interface fallback never needs the gate (popups
  record after it inside the same pass).
  🔴 When the pass is off, the host now says WHY: the console prints the
  refusal reason (shader streams not found / pass rejected) and that the
  interface fallback is drawing; when the pass stands it prints "overlay pass
  standing: the grid, the axes and the gizmo draw on the GPU".
- 🔴 SHADER DIRECTORY LESSON: `ShaderStreamDirectory()` resolves from the
  EXECUTABLE's own location (`GetModuleFileNameW` / `/proc/self/exe`), never
  from `current_path()` — a host launched from a shortcut or a console at
  another directory used to point at the wrong `Shader` folder, the pass
  refused on the missing streams, and the overlay silently never drew.
- 🔴 FILTER DROPDOWN LESSON: the Add-filter pill is clamped to the card's
  interior (`min(132 px, InteriorX)`); a narrow leaf shrinks the pill, it never
  overhangs the card's rounded edge. The harness's `editor-search-filter`
  asserts the card renders at its declared height (measured on real pixels).
