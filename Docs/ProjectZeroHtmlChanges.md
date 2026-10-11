# Project Zero HTML changes and C++ conversion

## Working agreement

- Make and review editor changes in `Experimental/ProjectZeroEditor` first.
- Keep this document as the cumulative change record for subsequent requests.
- Initial HTML-only gate was lifted on 2026-10-04: the user authorized C++ conversion and native build verification.
- All baking/export controls belong below authoring settings; native work must avoid stack-sized scene storage.
- Record browser verification separately from native verification. A browser check does not prove a C++ fix.
- Preserve the approved SolidArc and Fluid work.

## Change register

| ID   | Requested change                                    | HTML progress                     | C++ progress                                      |
| ---- | --------------------------------------------------- | --------------------------------- | ------------------------------------------------- |
| C001 | Native-style Construct presentation                 | Implemented; awaiting user review | Deferred until the HTML review is finished        |
| C002 | Ctrl+A opens Construct; Shift+A must not open it    | Implemented and browser-checked   | Implemented; native shortcut checks pass               |
| C003 | Blank checkerboard with constructed representations | Implemented and browser-checked   | HTML-only preview, not a renderer change          |
| C004 | Distinct filled inspector quick-action icons        | Implemented and browser-checked   | Deferred until the HTML review is finished        |
| C005 | Redesigned viewport header                          | Implemented and browser-checked   | Deferred until the HTML review is finished        |
| C006 | SolidArc-style XYZ transform table                  | Implemented and browser-checked   | Deferred until the HTML review is finished        |
| C007 | Object-bound full material channels and sources     | Implemented and browser-checked   | Native slab and producer integration deferred     |
| C008 | Dockable ShaderEditor and shader-ball preview       | Implemented and browser-checked   | Native shader execution and BSDF preview deferred |
| C009 | Matching top/bottom drawers and page-close gestures | Implemented; mouse/touch checked | Reported native drag problem recorded; mirror deferred |
| C010 | Material, imported-file and engine asset browser | Implemented and browser-checked | Native asset pipeline integration deferred |
| C011 | Viewport-only settings/debug menu | Implemented and browser-checked | Native settings/statistics menu implemented; broader parity pending |
| C012 | Entity-relevant circular quick toggles | Implemented and browser-checked | Per-entity shadow/GI/physics bindings deferred |
| C013 | Precipitation icon instead of umbrella | HTML rain-cloud symbol implemented | Native artwork unchanged |
| C014 | Animated composite WindEditor | Implemented; evaluator and browser checked | Native wind field evaluation deferred |
| C015 | Cloud selection of a composite wind field | Implemented and browser-checked | Native cloud advection binding deferred |
| C016 | Local fog volume shape instead of fixed box bounds | Implemented and browser-checked | Native shape masks/SDF integration deferred |
| C017 | Permanent first Editor Camera | Implemented and browser-checked | Editor Camera pinned/locked; native feed checked |
| C018 | Interactive environment graphs | Implemented and browser-checked | Native Sun/atmosphere plots implemented; remaining graphs pending (C024) |
| C019 | AtmosphereLab atlas and space study | Implemented and browser-checked | Still to mirror; real native dome bake retained |
| C020 | Pill viewport header | Implemented and browser-checked | Existing native pill rail retained |
| C021 | Folder/collection inspector | Implemented and browser-checked | Native census, search and bounded paging; native roster cap unchanged |
| C022 | Resizable statistics/debug card | Implemented and browser-checked | Native card with engine intervals/completed GPU telemetry |
| C023 | Native conversion, bottom baking, stack safety | Approved HTML baseline retained | Initial native checkpoint; see remaining work below |
| C024 | Native Sun and atmosphere graph controls | Approved HTML baseline retained | Eight interactive plots; native input and low-stack checks passed |
| C030 | Polygon-only cliff shapes, bounded joints and local spalls | Implemented; geometry/browser checks passed | Not changed; HTML-only geological modelling pass |

## 2026-10-04 — C001: Construct presentation

### HTML changes

Replaced the two-column descriptive list with the structure drawn by
[`NativeConstructPanel.h`](../Engine/Editor/NativeConstructPanel.h):

- Compact Construct window, up to 860 × 760 pixels, with 12-pixel outer clearance on smaller displays.
- `CONSTRUCT / FRONTIER` heading and `01 Entities > 02 Properties` progression.
- Search across entity names.
- A 138-pixel category rail: All, Environment, Weather, Cameras, Geometry, Lighting.
- A multi-column, table-like entity matrix: 116-pixel cells, 8-pixel gaps, 52-pixel native icon artwork, names below the icons.
- Selecting a cell opens the matching inspector in the Properties step; Entities or Escape returns to the selection step.
- In the HTML preview, **Add to scene** confirms construction. Name and property edits are copied into the new outliner entry; Cancel does not create anything.
- Escape works while the search or a property has keyboard focus. Tab stays within the dialog; closing restores the opener's focus.

### Important native distinction for the later mirror

The checked native Construct window selects and activates **existing scene records**. Its source explicitly treats environment records as singletons. The HTML confirmation creates browser-local representations to exercise the requested workflow; this is not proof that native entity creation or environment duplication is supported.

When the C++ work is authorized, preserve those ownership rules and review creation versus activation deliberately. Do not copy the HTML duplication behavior blindly into native environment records.

## 2026-10-04 — C002: Construct shortcut conflict

### Observed source and desired behavior

The user wants Ctrl+A for Construct and Shift+A for fast leftward camera movement.
The checked [`EditorHost.cpp`](../Engine/Editor/EditorHost.cpp) currently opens Construct on **Shift+A**, guarded by `!Steering_` and `!WantTextInput`. Its comment already describes the overlap with boost + strafe left. The former HTML shortcut also used Shift+A.

### HTML fix

- Ctrl+A opens Construct outside editable controls. Cmd+A is supported as the corresponding browser shortcut.
- Shift+A and Shift+W/A/S/D do not open Construct; Shift+A is not consumed by the Construct shortcut.
- Extra Shift/Alt modifiers and repeated keydown events do not trigger this command.
- Ctrl+A retains ordinary Select All behavior in inputs, textareas, selects, and editable content.
- Another open dialog is not bypassed by the global Construct shortcut.

No HTML flight-camera implementation is claimed: the movement chord is left available rather than repurposed.

### Deferred C++ action and acceptance checks

- Replace the Shift-based Construct predicate with the deliberate Ctrl+A chord, while retaining appropriate text-entry and camera-input guards.
- Audit displayed shortcut hints and all other native Construct entry points.
- Verify Ctrl+A opens once, held Shift+A never opens it, and WASD + Shift keeps moving the camera, including entering/leaving steering mode while modifiers remain down.
- Verify Select All in text entry and Escape in both Construct steps.
- **Native implementation, build, and runtime verification are pending explicit authorization after the HTML review.**

## 2026-10-04 — C003: Checkerboard construction preview

- Replaced the static native reference image with a blank, dark checkerboard.
- Each confirmed construction adds a labeled analytical symbol: box, sphere, cylinder, cone, ring, camera outline, light circle, weather symbol, or a circle/label fallback.
- All 23 catalogue entries have a representation. These are SVG previews, not meshes, physics, GPU lighting, or astronomical rendering.
- Selecting a symbol selects the same entry in the outliner and inspector.
- Renaming, visibility, collection visibility, duplication, removal, and browser-local persistence operate on the same entries.
- Generated names are unique; cancelling the Properties step leaves the checkerboard unchanged.
- The original reference outliner is retained for inspector coverage. Only newly constructed preview entries appear on the checkerboard; the viewport footer reports those entries, not the old reference-frame triangle count.
- The blank checkerboard and analytical symbols are **HTML-only stand-ins**. Do not replace the native editor's actual renderer with them during the later mirror.

## Browser verification

Executed on 2026-10-04 using headless Chromium, without building or changing native C++:

- `CheckConstruction.mjs`: all 23 catalogue entries construct selectable symbols; shortcut exclusions; text-field Select All; Escape; six categories; empty search; cancel; carried property edits; unique names; rename, visibility and collection visibility; duplicate/remove; reload persistence; 1024/1280/1440/1920-pixel widths. No browser errors.
- `CheckBrowser.mjs`: existing regression covering 18 inspector sheets, 121 numeric edits, pointer dragging, tab docking and reopening, Control Centre pages, and moon selection. No browser errors.

Visual proofs:

- [Construct matrix](../Experimental/ProjectZeroEditor/Screenshots/ConstructMenu.png)
- [Construct properties](../Experimental/ProjectZeroEditor/Screenshots/ConstructProperties.png)
- [Blank checkerboard](../Experimental/ProjectZeroEditor/Screenshots/Checkerboard.png)
- [Constructed symbols and selection](../Experimental/ProjectZeroEditor/Screenshots/ConstructedScene.png)

## 2026-10-04 — C004: Filled inspector quick actions

- Replaced the repeated small power/orbit glyphs with filled semantic SVG silhouettes. Each action within a quick group has a distinct symbol: fog, wind, cloud, rain, collision, stars, twinkle, sun, disc, orbit, motion, pause, baking, baked image, and optical effects.
- Enabled icons follow the inspector context: fog layers, a cloud, falling rain, or a rainbow rather than the same generic power icon.
- Larger icon seats, rounded tiles, and explicit **ON / OFF / UNAVAILABLE** captions use green, muted red, and grey respectively. Status is conveyed by text as well as colour.
- Retained existing property callbacks, keyboard activation, and native-unavailable disabled states. Styling does not enable baking, imported-image playback, or any disconnected native control.
- Original outliner artwork, inspector graphs, card styling, notch, and trapezoidal document tabs remain intact.

## 2026-10-04 — C005: Viewport header

- Replaced the scattered single-row controls with two grouped rows: scene identity / constructed count / preview badge / Edit–Sim–Play, followed by Construct / frame selected / projection / split / diagnostics / Control Centre.
- Construct is the primary action, with its Ctrl+A hint at wider sizes. Compact layouts remove secondary wording rather than hiding tool buttons. Very narrow dock widths allow the action row to wrap.
- Projection and split view are independent controls; switching the projection preference does not close split view. Projection remains a UI preference for the analytical 2D preview, not a live camera.
- Split, diagnostics, and mode buttons expose their state through `aria-pressed`. F3 / Shift+F3 diagnostics remain synchronized with the toolbar.
- Frame selected is disabled for reference-only or effectively hidden entities. It targets the selected constructed marker, not an engine camera.
- Sim and Play still disclose that no native simulation is connected. The scene counter reports browser-local constructions, not engine entities.
- C++ presentation, icons, and behavior are deliberately **not mirrored yet**.

### Browser verification for C004–C005

Executed on 2026-10-04 using headless Chromium:

- `CheckQuickTools.mjs`: 32 quick-tile instances across 12 inspector sheets; semantic filled SVGs; distinct symbols within each quick group; click and Space activation; ON/OFF/unavailable captions; independent split and projection; diagnostics button plus F3/Shift+F3; exclusive mode selection; Control Centre open/close; Construct and frame eligibility.
- Toolbar bounds and hit-testing checked at 1024, 1280, 1366, 1440, and 1920 pixels wide, including a 1366 × 608 content area. Screenshots wait for the existing notch resize animation to settle.
- `CheckBrowser.mjs` and `CheckConstruction.mjs` rerun successfully, including all 18 inspector sheets, 121 numeric edits, and all 23 construction entries. All three suites reported no browser errors.
- No C++ files changed, native build performed, or native runtime result claimed.

Visual proofs:

- [Fog and wind filled quick icons / viewport header](../Experimental/ProjectZeroEditor/Screenshots/QuickTilesFog.png)
- [Stars: distinct symbols and ON/OFF/unavailable states](../Experimental/ProjectZeroEditor/Screenshots/QuickTilesStars.png)
- [Compact 1024-pixel toolbar](../Experimental/ProjectZeroEditor/Screenshots/Toolbar1024.png)

## 2026-10-04 — C006: Transform table

- Replaced the geometry inspector's read-only position / rotation controls with a compact SolidArc-style table.
- Position, rotation and scale occupy rows; X/Y/Z occupy columns, with units and axis colours.
- Local cloud and local fog bounds use the same table for world-space Centre / Half Size, preserving their native defaults.
- Supports numeric entry, horizontal value scrubbing, Shift precision, row resets, and the existing instance lock.
- Transform drafts are per object and survive selection changes and browser reloads.
- These values are **HTML authoring state**, not native transform matrices or modifications to SolidArc geometry.
  The checkerboard remains an analytical placement preview, not a transformed mesh renderer.

## 2026-10-04 — C007: Object-bound material channels

### Presentation and ownership

- Replaced the geometry inspector's small Surface block with object-bound Material channels.
- Used the supplied traffic-dashboard image for rounded charcoal cards, light typography, pill selectors,
  understated borders, status counters and restrained colour accents. Preserved the existing editor shell.
- Shows all 20 rows from `Engine/DisplayPresentation/MaterialInspector.cpp`: base colour, metallic, roughness,
  reflectance/IOR, orientation, occlusion, emission, opacity, anisotropy, anisotropy direction, coat, coat roughness,
  coat orientation, sheen colour, sheen roughness, subsurface colour, thickness, transmission, refraction IOR,
  and displacement. A searchable list selects the channel editor.
- Material drafts belong to the selected geometry object's surface slot 0. The separate ShaderEditor remains pinned
  to that owner when outliner selection changes; its target selector deliberately changes the owner.
- This does not claim discovery of native shared material assets or multiple resolved submesh slots.
  Non-surface environment / light / camera inspectors retain their existing domain-specific controls.
- Reflectance and refraction share the same IOR carrier, matching the inspected native slab semantics.
- Added Standard, SSR, Glass, Fabric, Metal, Clear coat, Subsurface, Emissive and Unlit profiles.
  Profiles adjust fill defaults while retaining authored gradient, texture and code drafts.
- **SSR is explicitly identified as a reflection route, not a BSDF or a working browser ray-tracing mode.**

### One active source per channel

- **Fill:** typed colour or scalar value, with hex entry, numeric limits and sliders as appropriate.
- **Gradient:** 2–8 stops; add/remove; pointer dragging; keyboard movement; explicit stop position and colour/value;
  U/V direction; linear interpolation. Scalar channels interpolate scalar values rather than RGB colours.
- **Texture:** local PNG/JPEG/WebP selection, replace/remove, RGB or component sampling, and UV repeat.
  Inputs are limited to 8 MB. A maximum 160-pixel image retaining alpha is embedded for the browser preview;
  this is not the original full-resolution texture or a native resource descriptor. No external upload occurs.
- **Code link:** C++, Slang/HLSL or GLSL provider, symbol/entry point, source/integration notes, and stored fill fallback.
  These are unresolved descriptors. No text is evaluated, compiled or executed by the HTML editor.
- Switching sources preserves the other source drafts. Material state and bounded texture previews use the existing
  per-object save/export data. Storage failures now show an explicit warning instead of silently failing.
- Restored material values are constrained to the channel schema; embedded image sources are restricted to data URLs.

## 2026-10-04 — C008: ShaderEditor window and preview

- The material card's expand arrow opens a separate, movable, resizable **non-modal window inside the HTML app**.
- Dock buttons attach it to Left, Centre or Right. The docked ShaderEditor uses the existing trapezoidal tab strip
  and tab drag/drop; Float window undocks it. It can be closed and reopened without discarding material data.
- Owner selection is explicit; missing/deleted owners show a recovery prompt instead of silently editing another object.
- Uses the existing `Exhibits/DistanceFieldGI/ShaderBall.mesh`, embedded by the standalone build: 35,897 vertices,
  67,832 triangles. Mesh provenance is shown in the preview details, following the native exhibit's identification
  of Pseudopode / UnityShaderBall as CC0. No replacement generated illustration is used.
- A browser-side software rasterizer displays the actual mesh with orbit and softbox/warm/cool lighting controls.
  Supported channel edits change preview pixels, including image UV sampling and gradients.
- Preview coverage: colour, roughness, metallic, IOR, occlusion, emission, opacity, coat, sheen, and approximate
  transmission. Orientation, anisotropy, subsurface and displacement are stored but not evaluated by this preview.
- **This is a lighting/authoring study, not the native BSDF, shader compiler, SSR renderer or native shader-ball exhibit.**
  Code-driven channels use their fill fallback until a real producer is connected later.

### Browser verification for C006–C008

Executed on 2026-10-04 using headless Chromium, without a C++ build:

- `CheckMaterials.mjs`: XYZ edit/scrub/reset/lock/persistence; all 20 typed channel rows and nine profiles;
  pinned ownership; shared IOR; channel search; signed scalar typing; gradient editing, limits and retained drafts;
  texture pixel changes, embedded data, failed-decode preservation and removal; code descriptors; floating movement;
  docking/tab dragging/undocking; non-overlapping docked cards; reload persistence.
- Material window layouts checked at 1024, 1280, 1366, 1440 and 1920 pixels wide without horizontal content overflow.
- `CheckBrowser.mjs`: all 18 inspector sheets, now 128 numeric edits, and existing shell/Control Centre regressions.
- `CheckConstruction.mjs`: all 23 catalogue entries, including material roughness carried from the new channel UI
  into the constructed object's inspector. Its selector was updated to the new channel editor, not bypassed.
- `CheckQuickTools.mjs`: prior filled-icon and viewport-toolbar regressions retained.
- All four suites reported no browser errors. No native source files, SolidArc or Fluid files were changed.

Visual proofs:

- [XYZ transform table](../Experimental/ProjectZeroEditor/Screenshots/TransformTable.png)
- [ShaderEditor with gradient and shader ball](../Experimental/ProjectZeroEditor/Screenshots/ShaderGradient.png)
- [Texture source and shader-ball response](../Experimental/ProjectZeroEditor/Screenshots/ShaderTexture.png)
- [Unresolved code producer](../Experimental/ProjectZeroEditor/Screenshots/ShaderCodeLink.png)
- [ShaderEditor docked in the main workspace](../Experimental/ProjectZeroEditor/Screenshots/ShaderDocked.png)

### Deferred native acceptance work

- Resolve real object/submesh/material ownership rather than copying the HTML slot-0 convention.
- Map every channel and its typed source to native slab descriptors; preserve shared IOR semantics and colour spaces.
- Map Glass/Fabric/Metal profiles to native material models and parameters; keep SSR in reflection configuration.
- Connect gradients, full-resolution textures, and authorized native code/shader producers through the appropriate
  resource and compilation paths. The browser's text descriptor is not a compiled or trusted native producer.
- Integrate native transform editing, native docking, and the native shader-ball renderer only after the HTML review
  is finished. Do not replace the engine BSDF with this lightweight browser preview.

## Next review

Await the user's review of this batch. Append subsequent requests to this document, keep their native status deferred, and obtain the final go-ahead before starting the C++ mirror.

## 2026-10-04 — C009–C011: Shared drawers, content library, viewport settings

### C009 — Reference and drawer interaction

Used the user-supplied repository-root [`UI.html`](../../UI.html) reference
(commit `d8a65f8`) for the top/bottom sheet interaction. Its generated demonstration
inventory was **not** used as engine content.

- Both notches now share `DrawerPanel.jsx`: closed, half-open and full-open stops;
  vertical handle dragging; horizontal handle repositioning; distance/velocity
  release snapping; fast flicks; and pointer cancellation.
- Open sheets can be dragged using their blank page surface, header or grip.
  Buttons, fields and independent scrolling regions keep their own interaction.
- Handles support click, Enter and Space; Escape closes the browser or returns
  through the Control Centre pages. Drawers are mutually exclusive and prevent
  interaction with the editor behind them. Focus is contained/restored.
- Settings dialogs and the library fit the visible half-height sheet; their
  contents scroll independently instead of moving the drawer.
- Real touch testing caught and fixed cancellation from bubbling
  `lostpointercapture` when implicit touch capture transfers to the drawer root.

**Native follow-up:** the user reports that their C++ build closes only through
handle presses, not by sliding the open page/handle. Record this as a required
native regression case, not as a browser fix proving the native issue resolved.
`ControlCentreHost.cpp` already contains `GrabSubject::Card` and `Grip` carry
paths. Actual hit routing/body ownership and the running build need verification
when the mirror is authorized; the root cause has not been established here.
No native source changes or native builds were made.

### C010 — Asset Browser

Added a bottom-notch content library with a dark catalogue-style layout,
All/Project/Imported/Engine filters, categories, search, grid/list presentation,
a selected-asset detail pane and previews.

The built-in library contains **179 entries**:

- 9 HTML material-authoring presets (not resolved native slab files).
- 6 embedded moon image previews.
- 2 actual DM Sans font resources, previewed using `FontFace`.
- 161 repository icons.
- The repository shader-ball mesh, shown through the existing browser preview.

Materials can be created, renamed, saved from the selected object, exported,
imported and edited in the pinned ShaderEditor. Editing an engine preset first
creates a project copy. The assignment selector chooses a geometry object;
applying a library material makes an **independent deep copy** rather than a
shared live binding. Later library edits/removal do not alter that object copy.
All previous material channels, source drafts and object-bound ownership remain.

File import keeps the original bytes in browser-origin **IndexedDB**. Images
receive thumbnails and previews; fonts can be previewed; other model/code files
can be catalogued, downloaded and removed, but are not executed or parsed into
native geometry. Reload and byte-identical original-image download were tested.

Limits and storage boundaries:

- 256 project/imported records, up to 32 files per batch, 16 MiB per file and
  2 MiB per imported material JSON. Browser quota limits also apply.
- Scene export contains metadata, thumbnails and material descriptions—not the
  IndexedDB original files. Moving to another browser/origin does not transfer
  those originals; unavailable files are reported. Download originals separately.
- Engine moon downloads are the embedded 512×256 previews, not the source 2K maps.
- Font preview does not change the editor's font. Code remains non-executed.
- The shader-ball remains an approximate browser preview, not native BSDF/SSR
  execution. There is no native asset pipeline or renderer binding in this work.

### C011 — Viewport settings

The viewport header gear no longer opens global Control Centre settings. It now
opens a viewport-only popup for the debug view and Hi-Z, alias and patch-error
controls. Diagnostics remain a separate toolbar toggle. The top notch remains
the entry point for the global Control Centre.

### Browser verification and proofs

Rebuilt the self-contained `index.html` (4.11 MiB). All five suites passed with
`Errors: []` on the final build:

- `CheckBrowser.mjs`: existing settings, scene persistence and inspector checks.
- `CheckConstruction.mjs`: Construct, shortcuts, representations and layouts.
- `CheckQuickTools.mjs`: quick tools and the new viewport-only settings popup.
- `CheckMaterials.mjs`: material channels/sources, ownership, docking and reload.
- `CheckAssets.mjs`: mirrored mouse gestures, real CDP touch, page-close,
  cancellation, flicks, half-height settings, catalogue scrolling, filters,
  create/edit/rename/assign/export, import/reload/exact-byte download/removal,
  invalid image rejection and 1024/1280/1366/1920 layouts.

Committed browser screenshots (not native screenshots):

- [Half-open drawer](../Experimental/ProjectZeroEditor/Screenshots/AssetDrawerHalf.png)
- [Material library](../Experimental/ProjectZeroEditor/Screenshots/AssetBrowser.png)
- [Engine images](../Experimental/ProjectZeroEditor/Screenshots/AssetImages.png)
- [Engine fonts](../Experimental/ProjectZeroEditor/Screenshots/AssetFonts.png)
- [Viewport settings](../Experimental/ProjectZeroEditor/Screenshots/ViewportSettings.png)

**Status:** HTML implemented and verified; awaiting user review. C++ mirroring
remains deferred until the HTML changes are finished and approved.

## 2026-10-04 — C012–C015: Relevant quick tiles and composite wind fields

### C012 — Replace the blanket Instance flags

Removed the generic `INSTANCE` tag card (Visible / Locked / Dynamic / Physics)
that was appearing on inappropriate entities. The replacement uses the existing
filled-icon quick-tile styling with **circular green/red icon seats**:

| Entity | Quick controls |
| --- | --- |
| Geometry | Visible, Dynamic, Cast shadows, GI, Physics, Locked |
| Local lights | Visible, Cast shadows, GI |
| Post Process | Enabled only |
| Collections / generic fallback | Visible only |
| Specialized environment panels | Their existing relevant controls; no blanket physics/dynamic card |

Dynamic off means static. Requesting physics turns Dynamic on; changing to static
turns physics off. Geometry locking retains the existing transform-edit lock.
Visibility continues to use the scene's actual HTML hidden-state map. Shadow/GI
participation and physics are **saved authoring flags**, not claims that the HTML
checkerboard runs a native renderer or rigid-body simulation. Existing stored
values on unsupported entities are not exposed as working capabilities.

### C013 — Precipitation symbol

Replaced the HTML outliner/Construct umbrella with a **filled rain cloud and
falling rain streaks**, tinted pale blue. This shares the precipitation quick
control's semantic symbol. Native icon files and the engine-resource catalogue
have not been rewritten; the native artwork mirror remains deferred.

### C014 — Wind preview and expanded WindEditor

Selecting a wind now shows a live canvas with vector arrows, a smoothly sampled
speed gradient, and animated particles. The expand button opens a two-view editor:

1. **Component placement:** an XZ map, numbered handles, influence-radius
   outlines, mouse/touch pointer dragging and keyboard position adjustments.
2. **Combined vector field:** actual evaluation of all enabled components,
   with independent vector/gradient/particle visibility and pause/resume.

The editor supports multiple separately named wind-field entities. Each field can
contain up to 64 independently named/enabled components:

- **Directional:** uniform background flow, strength and travel bearing.
- **Gust:** local directional flow with smooth radial falloff and a time-varying
  envelope controlled by frequency.
- **Tornado:** local tangential flow plus inward pull, with strength and radius.
- **Radial:** local outward flow with smooth falloff.

All enabled contributions are **linearly summed**, rather than merely drawing
unrelated arrows or choosing one component. Components can be created, selected,
renamed, moved, retyped and removed. Numeric drafts commit on blur/Enter, allowing
negative positions and normal typing of bounded values. Preview width/depth are
editable; directional flow is global, while local influence is controlled by
component radius. Component handles also support arrow keys (10 m per step).

Field descriptors live under `Values[windEntityId].WindField` and therefore use
the existing scene save/export/import mechanism. New fields receive distinct
scene IDs. Unmodified legacy wind speed/bearing/gust seed the initial prevailing
flow and gust. Legacy altitude/shear/veer/turbulence/steadiness descriptors remain
editable in a clearly marked native-draft card, but are not secretly included in
this horizontal evaluator.

**Scope:** this is a deterministic 2D browser authoring/evaluation preview, not a
3D fluid/tornado solver. Speed colours saturate at 30 m/s; particles are visualized
at 8× advection time. There is no vertical tornado simulation, native atmospheric
solver, native terrain interaction or native cloud renderer connection here.

### C015 — Cloud wind-field selection

Global and local clouds now choose a specific wind entity by stable ID through
**Wind binding → Wind field**. The binding previews the selected field's complete
component sum, not just its first component. `Follow Wind` gates the preview;
`None` explicitly means still air. Hidden fields produce disabled output, and a
removed assigned field is reported as missing instead of silently redirecting
the cloud to another field.

The binding's **Edit wind field** button opens that field in WindEditor without
changing ownership to the selected cloud. Edits stay on the wind entity, and
renaming it does not invalidate the cloud's ID reference. Native cloud advection
must consume this binding when the C++ mirror is authorized.

### Verification and evidence

Final standalone HTML build: **4.13 MiB**. All six browser suites passed with
`Errors: []`: `CheckBrowser`, `CheckConstruction`, `CheckQuickTools`,
`CheckMaterials`, `CheckAssets`, and the new `CheckWind`.

`CheckWind.mjs` covers finite evaluation, exact linear summation, radial falloff,
gust time variation, empty/disabled fields, entity-specific toggles, circular
seats, keyboard toggling, physics/static coupling, visibility persistence, the
precipitation symbol, animated pixels, numeric entry, component CRUD, mouse/touch and
keyboard placement, pause/resume, independent fields, cloud-bound editor
ownership, Follow Wind gating, reload, missing fields, still air, Escape/focus
restoration, and 1024/1280/1366/1920 layouts. Existing material tests retain the
transform lock regression using the renamed `Locked` tile.

Committed **browser** evidence:

- [Entity quick tiles](../Experimental/ProjectZeroEditor/Screenshots/EntityQuickTiles.png)
- [Selected wind preview](../Experimental/ProjectZeroEditor/Screenshots/WindInspector.png)
- [Expanded WindEditor](../Experimental/ProjectZeroEditor/Screenshots/WindEditor.png)
- [Actual browser field animation](../Experimental/ProjectZeroEditor/Screenshots/WindFieldAnimation.gif)
- [Cloud field binding](../Experimental/ProjectZeroEditor/Screenshots/CloudWindBinding.png)

**Status:** HTML review checkpoint. No C++ sources were changed and no native
build was run. Native entity capability binding, composite wind evaluation and
cloud advection integration remain deferred until the HTML review is finished.

## 2026-10-04 — C016–C017: Local fog shape and permanent Editor Camera

### Change-log continuity

The previous turn's C012–C015 quick-tile, precipitation-symbol, composite-wind and
cloud-binding changes were committed with this log in `728a579`. The older
`ed7d5df` URL visible in the user's screenshot predates those changes. This
section appends the new corrections; it does not replace or omit the prior log.

### C016 — Local fog takes the selected volume shape

Replaced the fixed `Local bounds` box graphic and editable half-size table with
**Fog volume shape**:

- Box / rectangular volume: width, depth and height.
- Sphere: radius.
- Ellipsoid and diamond / octahedron: width, depth and height.
- Cylinder and cone: radius and height, aligned with Z up.
- Regular polygon prism: radius, height and 3–16 sides.
- Custom polygon extrusion: an editable, ordered XY footprint and Z height.

The wireframe changes with the selected shape and dimensions. World-space Centre
remains editable. **Min, Max and Size are calculated from the actual selected
shape geometry**, including asymmetric custom footprints and polygon prisms;
they are read-only results, not an independent fixed-box authoring control.
The preview auto-fits the volume, while centre edits update the world-bound
readouts. Legacy local-fog `Half Size` values seed box dimensions, preserving
existing volumes on first load. New descriptors persist under
`Values[localFogId].FogShape` and travel with scene export/import.

Custom footprints support 3–32 vertices, including simple concave polygons.
Crossing edges, duplicate adjacent points, zero-area footprints and invalid
coordinates are rejected on Apply without replacing the last valid shape.
Restored descriptors are bounded/sanitized. **Custom currently means polygon
extrusion, not arbitrary imported mesh or SDF clipping.** Native general-mesh/SDF
support remains a separate integration task.

Local Fog's old density illustration is now explicitly labelled as a density
study, not a ray march through the selected volume. This work does not falsely
claim native shape-based fog rendering. Global/aerial fog and Local Cloud's
existing bounds controls are unchanged.

### C017 — The first camera belongs to the editor

The original reserved `camera` entry is now **Editor Camera**, retaining its ID
and optics drafts rather than allocating a second replacement entry. It is the
first camera in the Cameras collection and carries an `EDITOR` badge and an
inspector explanation distinguishing it from scene render cameras.

- Editor Camera cannot be removed, hidden or renamed through context menus,
  outliner visibility controls or F2.
- Scene structure normalization guarantees exactly one reserved editor camera
  after startup, reload, scene import, reset and structural edits. Imports with
  no editor camera—including an empty scene—restore it. Spoofed or duplicated
  reserved rows cannot turn it into a deletable scene entity.
- Construct → Main Camera adds a normal scene camera. Duplicating Editor Camera
  also creates a normal Main Camera with a fresh ID, never a second protected
  editor camera. Ordinary scene cameras remain deletable and renameable.
- Empty-scene import remains valid and retains supplied editor optics settings.

This is an HTML editor ownership/identity policy. It does not implement or change
native viewport navigation, camera switching or the native scene lifecycle.

### Browser verification and proofs

Standalone HTML rebuilt at **4.14 MiB**. All seven suites passed with
`Errors: []`: `CheckBrowser`, `CheckConstruction`, `CheckQuickTools`,
`CheckMaterials`, `CheckAssets`, `CheckWind`, and the new `CheckFogCamera`.

The new suite checks all eight shape descriptors, finite geometry and derived
bounds, legacy half-size conversion, sphere/cylinder extents, prism topology,
custom polygon editing/rejection, centre offsets, scene round-trip, reload and
1024/1280/1366/1920 layouts. Camera checks cover disabled removal/hide/rename,
F2, duplicate and Construct semantics, empty/missing-camera imports, retained
optics and reserved-camera restoration after corrupted/missing stored rows.
Existing camera browser checks now target Editor Camera instead of the old
Main Camera baseline label; Construct continues to target Main Camera.

Committed browser screenshots:

- [Sphere fog shape and derived bounds](../Experimental/ProjectZeroEditor/Screenshots/FogSphere.png)
- [Triangular prism fog](../Experimental/ProjectZeroEditor/Screenshots/FogPrism.png)
- [Custom polygon volume](../Experimental/ProjectZeroEditor/Screenshots/FogCustom.png)
- [Protected Editor Camera](../Experimental/ProjectZeroEditor/Screenshots/EditorCamera.png)

**Status:** HTML review checkpoint. No C++ sources changed and no native build
was run. Native fog-shape masks and permanent editor-camera ownership/removal
rules remain deferred until the HTML review is finished.

## 2026-10-04 — C018–C019: Live environment graphs and AtmosphereLab

### Change-log continuity and scope

C016–C017 were published at `e5f1dbf`; the user's `728a579` screenshot predates
that checkpoint. This update retains the existing environment cards, fog-shape
editor, permanent Editor Camera, wind/cloud bindings, material editor and
geometry workflow. It changes the HTML prototype only. **No C++ sources were
changed and no native build was run.** Materials and geometry do not receive
this new card/graph treatment.

### C018 — Actual data in the existing cards

- **Height Fog:** replaced the unrelated decorative density and transmission
  illustrations with interactive sampled curves. Both use the same altitude
  and distance probe and exponential model:
  `sigma(z) = Density * exp(-max(0,z) / max(10,Falloff Height))`,
  `T(d,z) = 100 * exp(-sigma(z) * d)`. At density 0.02/m, height 2 m,
  falloff 400 m and distance 200 m, transmission is approximately **1.868%**,
  not the old unrelated 92% readout. Disabled fog gives zero extinction and
  100% transmission. The visibility graph represents a horizontal path at the
  selected height, not an integrated sloping ray.
- Drag or touch a graph to set its probe; hover to inspect; arrow keys and
  Home/End provide keyboard control. Density and visibility share saved probes.
  Numerical probe inputs remain available. SVG chart dimensions follow the
  actual card width, keeping axes readable in the narrow inspector.
- **Local and aerial fog:** visibility studies now respond to authored settings.
  Local fog uses an explicitly illustrative `0.01 * Density * Coverage` /m
  coefficient; aerial fog uses `0.001 * Density` /m after Start. These are HTML
  authoring approximations, not recovered native scattering coefficients.
  Local visibility is not ray-marched through the selected shape. Aerial
  spectral transmission now responds to Density, Mie Blend and the shared
  distance probe; its 550 nm sample agrees with the visibility graph.
- **Sun:** the existing daylight card now samples the existing NOAA-style solar
  solver across 24 hours, rather than drawing an unrelated parabola. The
  existing Dynamic settings card gains a 2026 local-noon seasonal curve; dragging
  it edits Month and Day of Month. Sunlight intensity plots `Intensity * Direct`
  as relative gain, explicitly not measured lux. Temperature gains a normalized
  visible Planck spectrum and wavelength probe; RGB mode explicitly treats this
  as the temperature draft, not the active RGB tint's spectrum. No duplicate Sun
  cards were introduced.
- **Atmosphere:** scattering, aerosol haze, ozone absorption and density falloff
  cards now contain interactive wavelength/distance/altitude studies connected
  to their controls. The ozone Gaussian and aerosol attenuation are labelled
  illustrative, not native LUT or measured spectroscopy results.
- **Clouds:** replaced the old generic exponential sketch with an interactive
  vertical density envelope using actual base/centre, thickness, density and
  coverage. The sine-squared profile is explicitly an unlit authoring study,
  not a native volume-field sample. Local Cloud's bounds are now projected from
  its actual Half Size; X/Y/Z handles resize those native fields and derived
  world Min/Max/Size readouts follow Centre. The existing Volume section card
  shows the density study rather than repeating the same fixed box drawing.
  The separate GPU-cloud-shadow controls also receive a live parameter map;
  unavailable runtime march/tap counters remain unavailable.
- **Other cards:** numeric and colour controls are shown in collapsible,
  directly editable SVG parameter-column maps, with exact native-valued
  readouts. Heights represent each control's fraction of its declared range;
  these are authored data, not simulated telemetry. RGB columns edit the
  actual hex colour (including Ground reflectance). This covers environment,
  camera, light and post-processing cards while excluding materials/geometry.
  Existing useful previews, controls, disabled native actions and cards remain.
- Rounded, quieter dark surfaces and subdued fractional digits follow the
  supplied dashboard references, particularly `62c17c83ea725348903581db2a824349`.
  Styling is scoped rather than changing the material/geometry layout.

### C019 — Reconstructed 2D atlas and a space-view study

Searched the current Experimental and broader Frontier source tree for the old
Arctic/smog/Mars atlas, but did not recover its implementation. **This is a
reconstruction, not a claim to have restored the historical atlas or its entire
inventory.** The UI says so explicitly.

A single coloured 2D map plots **Rayleigh strength (0–4)** against **Mie strength
(0–6)**. Its nine representative presets are Arctic clear, Alpine, Temperate
Earth, Tropical humid, Desert dust, Urban haze, Thick smog, Mars-like dust and
Dense golden haze. The colour field is an inverse-distance authoring palette,
not a physically simulated sky colour or exhaustive atmospheric taxonomy.

Named dots/buttons apply complete presets, including scale heights, tint,
ground reflectance, ozone, anisotropy and atmosphere extent. Switching back
from Mars-like restores Earth radius/other defaults instead of leaving stale
planet settings. A preset is active only while its associated settings match.
Dragging empty map space or using its arrow keys edits Rayleigh/Mie and marks
it Custom. Atlas pointer coordinates use the SVG screen transform, including
letterboxing in small cards and compact workbench layouts.

**Atmosphere from space** renders a CPU spherical, single-scatter study with
Rayleigh/Mie terms, approximate ozone absorption, a planet shadow and a
procedural surface. It responds to atmosphere settings, ground colour,
observer altitude, orbit and a preview-only sun angle. It is not the native sky
renderer, real planet imagery, a multiple-scattering solution or a composition-
accurate Mars model. Default limb exaggeration is **8×**, visibly labelled;
1× is available. The preview sun angle is independent of the scene Sun.

Drag/arrow keys orbit; Animate/Pause uses one shared clock between the inspector
and expanded workbench. **Expand AtmosphereLab** presents the atlas and space
view together. The modal makes the editor inert, traps Tab, closes with Escape
and restores focus. It fits the tested desktop widths, with a compact treatment
at 1366×768. Background preview drawing is skipped while the workbench is open.

Probe values, atlas-applied properties and orbit/observer settings use the
existing scene Values/save path. Planet radius and preview controls are HTML
study descriptors, not newly implemented native fields. Baking/LUT fetching
remains explicitly unavailable; no fake bake results were added.

### Verification and committed proofs

Rebuilt the standalone `index.html` (**4.16 MiB**). All **eight** browser suites
passed on the final build: `CheckBrowser`, `CheckConstruction`, `CheckQuickTools`,
`CheckMaterials`, `CheckAssets`, `CheckWind`, `CheckFogCamera`, and the new
`CheckEnvironmentGraphs`.

New checks cover shared fog equations/probes and disabled state, real touch
input and endpoint clamping, mouse/keyboard edits, solar date/time response,
atlas preset completeness and free-map edits, changed canvas pixels for
presets/orbit, animation, modal Tab/Escape/inert/focus restoration, RGB edits,
reload persistence, native local-cloud extent edits, aerial spectral response,
cloud-shadow graphs, and exclusion of geometry/materials. Workbench layout
checks cover 1024/1280/1366/1920 widths; a separate real 1366×768 proof is included.

- [Height Fog — live visibility probe](../Experimental/ProjectZeroEditor/Screenshots/HeightFogVisibility.png)
- [Height Fog — altitude density study](../Experimental/ProjectZeroEditor/Screenshots/HeightFogDensity.png)
- [Sun — seasonal graph in the existing card](../Experimental/ProjectZeroEditor/Screenshots/SunSeasonalGraph.png)
- [AtmosphereLab — atlas and Arctic space study](../Experimental/ProjectZeroEditor/Screenshots/AtmosphereAtlas.png)
- [AtmosphereLab — actual 1366×768 layout](../Experimental/ProjectZeroEditor/Screenshots/AtmosphereDesktop.png)
- [AtmosphereLab — Mars-like preset](../Experimental/ProjectZeroEditor/Screenshots/AtmosphereMars.png)
- [Local Cloud — live bounds and derived extents](../Experimental/ProjectZeroEditor/Screenshots/LocalCloudBounds.png)
- [Environment graph test report](../Experimental/ProjectZeroEditor/Screenshots/EnvironmentGraphs.json)

**Status:** HTML review checkpoint. Native integration, physical calibration,
real volume sampling and native atmosphere rendering remain deferred.

## 2026-10-04 — C020–C022: Header pills, collection inventory and performance cards

### Change-log continuity

The C018–C019 environment graphs and reconstructed AtmosphereLab were published
at `8b13c12` and accepted as looking better. This update retains those cards and
adds the requested header, folder and debug-view changes. It remains HTML-only:
**no C++ sources changed, no native build run, no native timing values invented.**

### C020 — Pill-shaped viewport header controls

Construct, frame-selection, projection, split, diagnostics/settings and the
Edit/Sim/Play controls now use pill rounding. Icon-only actions use short
capsules, with compact sizing in narrower viewports. Existing actions, keyboard
shortcuts, disabled states and pressed-state styling are retained. Document
tabs keep their existing trapezoidal shape; materials and geometry inspectors
are not restyled by this change.

### C021 — Informative folder/collection inspector

Replaced the generic folder panel's static `5 direct · 14 total` readout with
an inventory derived from the actual scene hierarchy:

- Collection name, description, ancestor breadcrumbs and stable identity.
- Total descendant entries, direct children, nested folders and maximum depth.
  Counts include folders and entity attachments, not only geometry leaves.
- Effective visible/hidden counts, including ancestor visibility. The protected
  Editor Camera remains visible. The two visibility cards also filter contents.
- Proportional type-distribution bar and clickable type/count legend. Constructed
  preview marker counts are separate; these are scene records, not renderer
  workload, resident memory, triangle counts or file-size claims.
- Search by name, ID or description; direct-child/all-descendant scope; type
  and visibility filters; name/type/depth sorting; 25/50/100-entry pages. Only
  the current results page is mounted, rather than thousands of inspector rows.
- Each result shows its type, hierarchy level, parent and visibility reason.
  Open an entry to inspect it, expand its ancestors and clear outliner filters
  so the selected row is discoverable. Per-entry visibility uses the existing
  protected-camera policy; an inherited hidden state is explained rather than
  presented as an effective visible toggle.
- Empty/no-match states, filter reset, persistent notes and the existing Tint
  draft. Tint now colours collection accents only, not child objects/materials.

`FolderInventory.mjs` builds indexed parent/identity maps and traverses
iteratively with a cycle guard. The outliner also reuses the child index and
iterative traversal, replacing per-node full-array scans/recursive descent.
The existing outliner remains a tree, not a newly virtualized asset database;
the bounded-page guarantee applies to the new folder contents list.

### C022 — Movable, resizable performance/debug card

The diagnostics header button and existing F3 route now open a rounded dark
performance card, following the supplied dashboard references. Large metrics,
subdued fractional digits, restrained accents and real history replace the
old monospace diagnostic block.

Metric choices:

- **Frame rate / FPS:** actual browser `requestAnimationFrame` cadence.
- **Frame interval / ms:** measured callback intervals, not CPU execution or GPU
  render duration. Half-second samples report mean interval/FPS and latest
  interval p95; window statistics report mean and low/high sampled values.
- **GPU timing:** a clearly unavailable state and unpopulated stage timings.
  There is no native GPU timing connection in this HTML app, so no synthetic
  values or timing graph are shown.
- **Scene counts:** actual scene records, visible/hidden counts, folders and
  constructed preview markers. These are not draw calls or memory telemetry.

Choose **Graph + stats**, **Graph**, or **Stats**, and 15/30/60-second history.
Hover or use arrow/Home/End keys on the graph to inspect recorded samples.
Pause/Resume freezes the browser sample snapshot, not the scene authoring state.
The independent FPS badge now uses the same browser timing sampler. Sampling
runs only while subscribed, and visibility-change handling resets timing
intervals so a suspended/background tab is not counted as one giant frame.

Drag the header to move the card; drag its lower-right handle to resize. Both
handles accept arrow keys (10 px; Shift = 30 px). Layout stays inside the
viewport and adapts when the viewport shrinks. Metric, presentation, history
window, position and size persist in `Frontier.DiagnosticsCard.v1`; live timing
history itself does not persist. Header reset restores the default layout.
Pause/history controls and the resize handle remain in the fixed card footer;
content scrolls when the card is small. Close/Escape restores the diagnostics
button's focus.

Existing native-buffer selections and F3/Shift+F3, F4, F5 and F6 controls remain
available in viewport settings and the card's expandable native-debug details.
They are still selections only, not rendered native buffers. F3 cycles those
buffer selections; the card's metric selector changes its statistics view.

### Browser verification and proofs

Standalone HTML rebuilt at **4.19 MiB**. All **nine** browser suites passed:
`CheckBrowser`, `CheckConstruction`, `CheckQuickTools`, `CheckMaterials`,
`CheckAssets`, `CheckWind`, `CheckFogCamera`, `CheckEnvironmentGraphs`, and the new
`CheckWorkspaceCards`.

New coverage includes pill geometry, actual collection counts, inherited
visibility, persistent notes/tint, a generated **2,500-record browser fixture**,
search/filter/page-size changes, bounded result DOM, drill-through, camera
protection and empty state. A separate pure-model test traverses a
**10,000-level hierarchy** without recursive stack growth (24.9 ms in this
sandbox run; not a native performance guarantee).

Diagnostics checks exercise actual callback samples, deliberately induced
browser main-thread stalls, history probing, pause/resume, metric/presentation
switches, unavailable GPU state, mouse/keyboard movement and resizing,
close/reopen persistence, Escape from a focused select and viewport bounds at
1024/1280/1366/1920 widths. Existing environment, material, wind, fog-shape,
assets, construction and permanent-camera regression suites also pass.

Committed screenshots and report:

- [Folder inventory and resized FPS card](../Experimental/ProjectZeroEditor/Screenshots/FolderDiagnostics.png)
- [Frame-interval graph](../Experimental/ProjectZeroEditor/Screenshots/FrameTimingCard.png)
- [GPU timing — explicit unavailable state](../Experimental/ProjectZeroEditor/Screenshots/GpuTimingUnavailable.png)
- [Large collection summary — generated browser test records](../Experimental/ProjectZeroEditor/Screenshots/LargeCollection.png)
- [Searchable/paginated collection browser — same fixture](../Experimental/ProjectZeroEditor/Screenshots/CollectionBrowser.png)
- [Workspace-card test report](../Experimental/ProjectZeroEditor/Screenshots/WorkspaceCards.json)

**Status:** published HTML review checkpoint. Native GPU/CPU frame telemetry,
renderer-buffer visualization and C++ UI mirroring remain deferred.


## 2026-10-04 — C023: Native conversion begins; baking and stack safety

**This is a partial native checkpoint, not a claim that C001–C022 are all ported.**
The approved HTML baseline remains `95cfb8e68ad25b8aca17d9ee0d5421d168ec19a2`.
User authorization now includes headless native execution, Windows/MSVC and stack checks.

### Implemented in the native editor

- C002: Ctrl+A opens Construct, with no repeat; Shift+A stays available for fast flight.
  Text editing, steering and conflicting modifiers are excluded.
- C011: the viewport gear opens a settings menu, including Statistics/Debug and rendering settings.
- C017: the stock first camera is named Editor Camera and is pinned/locked; generic inspector identity,
  visibility and lock controls cannot change a pinned record. This retains the native camera owner.
- C021: native folder inspector displays real roster totals, direct contents, depth, inherited visibility,
  protected counts, tint and notes. Search, category/visibility narrowing and 25/50/100-row pages are bounded.
  Selecting a result reveals it in the outliner, clearing search/category exclusions and opening enclosing rows.
  The runtime's existing 1,024-row capacity is unchanged; the larger traversal proof is explicitly a fixture.
- C022: native movable/resizable Statistics/Debug window, FPS/frame-interval/GPU selection,
  stats/graph modes, pause/clear, 15/30/60-second sample windows and graph inspection.
  Frame intervals come from the engine clock; GPU values use valid completed timestamp telemetry, not an estimate.
  The old renderer detail rows and F3–F6 shortcuts remain available.
- Sun, Atmosphere/Sky, Lens Flare, Stars and Rainbow baking/export sections now follow properties.
  Actual atmosphere pixels and all their preview selectors move with the bake section.
  The atmosphere request still reaches the renderer queue; Lens Flare still exports the real HDR image.
  Unsupported Sun/Stars/Rainbow targets and flare scene playback remain explicitly unavailable, not fake successes.

### Stack work and verification

- Runtime scheduler/editor, celestial owner and selected sheet are constructed directly in heap-backed storage.
  The sheet alone is 19,392 bytes and the current editor owner is 37,040 bytes on this Linux build.
  No stack reserve increase was used.
- Removed recursive scene-roster traversal and repeated whole-scene scans during traversal. Heap-backed
  adjacency and an explicit pending list preserve preorder and terminate cyclic links through visited records.
  Roster depth is carried by the traversal rather than truncated to eight enclosing links.
- Actual compiled native proof executes under a **262,144-byte Linux stack limit**, including a 10,000-level
  collection fixture, a 10,000-placement real SceneStructure, native camera protection, Construct shortcuts,
  a pointer click on the relocated atmosphere bake button and a 256 × 512 × RGBA16F bake.
  The real `.environment` export/reload also passes with every HDR texel preserved exactly.
- GCC `-fstack-usage` gates the critical compiled UI/traversal functions at 8 KiB each. This is not a measurement
  of the complete Vulkan runtime entry point or a diagnosis of the user's historical crash.
- Native captures are CPU rasterizations of production ImGui commands, with real baked atmosphere pixels.
  Folder and timing records in the proof are labeled fixtures; the proof does not claim GPU execution.
- The native CMake proof route was repaired to compile the consolidated `Engine/Host` sources and current
  shading dependencies rather than removed Project-Zero source paths. Windows MSVC proof job added.
- All four Linux CTest targets passed: bootstrap safety, native renderer/billboards (113 assertions),
  native wind bindings, and the low-stack conversion proof. Final run: 181.17 seconds in this sandbox,
  using production source `28946b3c1ca7f4f1e552921ddbedc180dcdf59cf`.
- The renderer proof had stale assumptions: it expected removed global-system billboards and expected
  zero wind to stop independent density evolution. The proof now locates the actual Markers control by ID,
  tests only physical local-volume proxies, and separately verifies zero bulk velocity and continuing
  procedural evolution. No renderer behavior was changed to make those assertions pass.
- First MSVC proof attempt exposed missing `NOMINMAX` in the CMake proof configuration, not a runtime
  stack overflow. After that fix, MSVC exposed the headless target's accidental Vulkan device dependency:
  the debug-view label function lived in the device implementation. Labels now live beside their enum,
  without duplicating/stubbing any renderer function, and the proof no longer links device creation.
  A subsequent MSVC link identified missing environment codec/export translation units, now linked directly
  rather than stubbed. A Linux link without section garbage collection independently passed as well.
- **Windows/MSVC Release build and native conversion execution passed** on the same source commit:
  [run 37222602604, native editor job 111495857820][NativeMsvcProof]. This executes the real bake and
  export/reload with the default MSVC stack reserve, not an increased reserve. The 256 KiB limit and
  compiler-frame measurements above are Linux-only. The broader full-application job is still pending;
  this result does not claim a complete Vulkan application launch.

[NativeMsvcProof]: https://github.com/unassignedinbox/Slate/actions/runs/37222602604/job/111495857820

Proof sources, captures and compiler stack report: `VisualProof/ProjectZeroNative/` at repository root.

### Still to convert or reconcile

Full native parity still requires the HTML material-channel/source editor and dockable ShaderEditor,
asset-browser expansion and drawer gestures, remaining relevant quick tiles, the composite WindEditor,
local-fog shape masks in the actual renderer, the added interactive environment graphs, and the
AtmosphereLab 2D colour atlas/space study. Existing native environment inspectors and wind-component
bindings are retained, but they are not presented as implementations of the newer HTML composite model.
The new native statistics card also does not yet reproduce every browser-only keyboard/persistence detail.


## 2026-10-04 — C024: Native Sun and atmosphere graph controls

Second native conversion phase, continuing C018 without changing the approved HTML.
This phase replaces decorative studies inside the existing Sun and Atmosphere cards;
it does not add duplicate cards or move baking above authoring controls.

### Implemented

- Sun daylight plot samples the production `CelestialSolver` through 24 local hours.
  Dragging edits Local Hours; the native owner then solves the scene light direction.
- Seasonal plot samples that same solver at 12:00 local wall-clock time, not solar noon.
  Dragging edits Month and Day of Month. It uses the actual native year, UTC offset,
  latitude and longitude rather than a second hard-coded observer.
- Exposed existing native Year and UTC offset alongside the additional Sun settings.
  Calendar writeback clamps actual month lengths, including Gregorian leap-century rules.
- Sunlight gain plots Intensity × Direct and edits the real Intensity property.
  Units remain authored multipliers, not lux. Temperature contains a normalized visible
  Planck spectrum probe, explicitly not the active spectrum when RGB tint is selected.
- Atmosphere scattering retains the approved relative wavelength studies: Rayleigh power −4
  and illustrative aerosol power −1.3. Haze retains the normalized-path attenuation study;
  ozone retains the illustrative 600 nm Gaussian transmission band. These are labelled
  authoring illustrations, not native spectral coefficients, measured visibility or LUTs.
- Density plots both exponential scale heights up to the authored atmosphere extent.
  Native sliders continue through the existing medium writeback. Scattering and ozone
  share a wavelength probe; diagnostic probes do not edit medium settings.
- All eight plots support dragging, hover inspection, arrow keys and Home/End.
  Hover never writes a scene parameter. Diagnostic probe positions live in the inspector's
  UI-session storage; project-file persistence for these probes is not implemented.
- Curve sampling emits bounded line segments without automatic sample arrays or recursive calls.
  Existing baking, preview, export and unsupported-target states remain below the controls.

### Executed verification

Production source: `186f82446cbeb183ffd4e7272f33b880527be710`.

- Linux Release: **4/4 CTest targets passed**, 165.17 seconds total.
  The production CPU renderer still passes 113 assertions; wind and bootstrap checks also pass.
- Conversion execution passed with a **256 KiB stack** and the **8 KiB critical-frame gate**.
  GCC reports Sun inspector 1,840 bytes, Atmosphere inspector 704 bytes and proof entry 1,728 bytes.
  This remains a scoped native proof, not a diagnosis of every possible runtime stack failure.
- ImGui's optional item hooks are enabled only in the proof target. Checks locate actual
  submitted graph items and inject mouse/key input, rather than editing fields to fake a click.
  Pointer tolerances allow one native screen pixel; keyboard endpoints are checked exactly.
- Verified hover non-mutation, real gain/time/date writeback, resulting native solar-direction
  changes, every date in five common/leap/century years, February clamping, spectral and density
  equations, shared probes, and plot bounds in 900 px and 480 px inspector captures.
- Repeated the real bottom bake click, request consumption, heap-backed texture creation,
  `.environment` export/reload with exact HDR texel equality, and scrolling to baking.
- **Windows/MSVC Release compilation and NativeEditorConversion execution passed** for this source:
  [run 37224368522, native editor job 111500910577][EnvironmentMsvcProof]. The MSVC stack reserve
  remains unchanged; the 256 KiB restriction and compiler-frame report are Linux-specific.
  The broader application build for this source is still pending.
  The preceding source `28946b3` also completed its full Frontier/Project-Zero/Project-Drive
  build job successfully before this phase, independently of this phase's new checks.

Captures and execution reports remain in `VisualProof/ProjectZeroNative/`:
`SunGraphs.png`, `SunGraphsNarrow.png`, `AtmosphereGraphs.png`, `AtmosphereGraphsNarrow.png`,
`Verification.log`, `StackUsage.json`, `Regression.log` and `Provenance.json`.
They are actual native ImGui command rasterizations, not browser screenshots or Vulkan readbacks.
`AtmosphereGraphsDetail.png` is an unscaled crop of `AtmosphereGraphs.png` for convenient viewing.

### Still outstanding

C018 is not complete: the remaining fog/cloud studies, cloud bounds handles and generic
parameter-column maps still need reconciliation. AtmosphereLab's atlas/orbital study,
materials/ShaderEditor, asset drawers, composite wind and renderer fog shapes are unchanged
and remain outstanding. This checkpoint does not claim full C001–C022 native parity.

[EnvironmentMsvcProof]: https://github.com/unassignedinbox/Slate/actions/runs/37224368522/job/111500910577

## C025 — SDF artifact reproduction and transport correction (2026-10-04)

### Scope and faithful CPU execution

The user has **not** retested the recent build on their PC. This work investigates their
previous reports of reflection-like diffuse lighting, unstable shadows, and wavy shadow
outlines. `VisualProof/DistanceFieldGI/RunDistanceFieldExecution.py` compiles and executes
the actual production SPIR-V and Vulkan stage on Mesa lavapipe/llvmpipe, with Vulkan
synchronization validation. It does not substitute a separately written CPU renderer.

The fixture supplies raster-style positions and primitive IDs on a 160 × 160 receiver
plane under a rectangular overhead surface. Its images are **diagnostic lighting maps**,
not perspective screenshots of the application or the user's scene. All comparison pixels
come from Vulkan readback, enlarged without filtering. `matte-reference.png` is separately
labelled independent rectangular-emitter irradiance quadrature, used only as an oracle.

### Production changes

- Interpolate unsigned corner distances rather than allowing unrelated nearest-face signs
  to cancel into false zero sheets. The original nearest-face sign was not a solid winding
  classification, particularly around open meshes.
- Refine shadow distance against actual triangles with a stackless BVH traversal; exclude
  the coplanar receiver and confirm central-ray occlusion exactly. Penumbra stepping no
  longer depends on camera-snapped clipmap cells. This is a geometry-refined shadow path,
  not a claim that coarse voxels alone can reproduce thin occluders reliably.
- Combine cosine hemisphere sampling with spatially selected **textured surface-card area
  sampling**, using matching solid-angle PDFs and balance-heuristic multiple importance
  sampling. Source cards retain authored textures, emission and cutout coverage; this is
  not a flattened lighting coefficient or a post-process blur.
- Keep the sampling sequence fixed in world-space transport. Remove the repeating
  sixteen-frame direction cycle in the radiance-cache update. Preserve Jacobi ping-pong
  and material/geometry history invalidation.
- Confirm diffuse traversal against the mesh if the distance-march budget expires.
- Replace the unconditional fixed-Fresnel mirror contribution with the authored base
  specular lobe, GGX visible-normal sampling and its PDF. Zero base specular weight no
  longer creates an unwanted mirror. Roughness, anisotropy and haziness affect this lobe.
  Full coat/fuzz secondary-reflection parity is not established by this correction.
- Skip solar shadow evaluation when solar radiance is zero.

A first attempt that only increased cosine-ray counts still showed stepped diffuse
silhouettes and **failed** the new quality gate. It was not accepted as the finished fix.

### Executed focused measurements

Baseline shaders: `a42147b1148ffa13f08d4d11f42b665a8afb1a0d` (unchanged production shaders).
Corrected shaders: `20409561bdf3fb8d37ec06c9bc14d921314e453b`.
Both were executed with the same fixture in run `37227335799`.
Errors below are in 8-bit red-channel display units, not linear radiance units.

| Measurement | Before | Corrected |
|---|---:|---:|
| Maximum camera-shift shadow RMS, fixed scene/light | 17.476 | 0 |
| Zero-specular reflection-toggle maximum delta | 106 | 0 |
| Symmetric scene's left/right diffuse RMS | 79.2705 | 4.23736 |
| Single-bounce error versus independent area integral | 83.1477 | 2.67571 |

The focused corrected execution passed its numerical gates and Vulkan validation.
The combined workflow exceeded its 20-minute job limit during the broader regression
phase; that is **not** recorded as a complete regression pass. Subsequent proof phases
are split into independent parallel CI jobs. The fixture also retessellates the same
emitter to exercise internal BVH branches and measure dependence on subdivision.

Evidence: `VisualProof/DistanceFieldGI/ArtifactBaseline/`, `ArtifactAfter/`, and
`SdfComparison.png`. Provenance includes source revisions, workflow IDs, shader hashes
and image hashes. Regression evidence has a separate provenance record.

### Completed independent verification

Final harness source: `00681169cb3e2d48067a3a63708b811dfc462245`; its production shaders
are unchanged from `2040956`. All three CPU Vulkan jobs passed in
[run 37228808856](https://github.com/unassignedinbox/Slate/actions/runs/37228808856):

- Baseline reproduction: **passed**, job `111513986487` (4m58s).
- Corrected artifact gates: **passed**, job `111513986626` (9m21s). The measurements above
  repeated exactly. Retessellating the identical emitter from two to six triangles,
  introducing internal BVH branches, changed the single-bounce map by **2.76445 RMS**,
  below the fixed 6-unit gate. The shadow and reflection-toggle maps are byte-identical
  across their respective comparisons, including all RGB channels.
- Full production SDF regression: **passed**, job `111513986649` (6m25s): textured material
  sampling and spatial card radiance, cutouts, emission/material revisions, unresident
  texture refusal, GI switching, moving geometry, camera re-snapping, clean restart,
  actual scene reflections, thin/solid glass and Beer attenuation, resize, and coplanar
  card continuity. Static frame RMS and restart maximum delta are both zero; coplanar
  card maximum neighbour delta is 1. **Zero Vulkan validation errors**, including
  synchronization and destruction checks.
- Windows/MSVC native editor/stack-safe conversion, project browser and Drive checks
  passed for the same harness revision. The broader Windows application build is still
  in progress; its completion is not required or claimed for these CPU-rendered results.

The numerical gates are enforced, not merely printed. Evidence and the cumulative
change record are retained on `arena/01a0fd48-slate`.

### Boundaries

This establishes the artifacts and focused corrections on the production software-Vulkan
path. It does not establish GPU frame time, driver parity on the user's hardware, or
artifact-free output for every scene. The refined shadow queries and additional transport
samples cost more than the old undersampled path; GPU performance remains unmeasured.
The fixture deliberately uses shadow cone slope 0.06; the application's existing default
softness and solar angular-size wiring have not been changed. This phase adds no editor
cards, changes no bake-control placement and does not close C018's remaining native UI work.

## C026 — Independent perspective SDF stress scene; broader fix is incomplete (2026-10-04)

The user requested actual Project Drive/material-grid renders, then selected an **independent custom scene**
when asked about the full shader-ball grid's SDF storage limits. This phase follows that revised request.
It does not claim to have rendered Project Drive or the full material grid.

### Method and captures

- Production shader source is unchanged from C025. Only the proof harness, workflow and evidence changed.
- Headless Vulkan host code was extracted without changing its implementation into `VulkanExecutionHost.h`.
  All three existing production SDF verification jobs passed again after this refactor.
- New scene: 292 triangles, ten instances, a ground plane, coloured walls, stairs, a smooth-normal sphere,
  a movable box, a thin post and a warm emissive ceiling panel. Materials are matte Lambertian with no maps.
  Reflections are disabled to isolate indirect diffuse artifacts. Unit placeholder specular tables are unused
  by these zero-specular/zero-coat/zero-fuzz materials; this is **not** a full material-lobe parity proof.
- Double-precision CPU triangle intersections produce perspective primary positions/primitive IDs from the
  same float geometry submitted to the stage. All material evaluation, SDF construction, card capture,
  radiance propagation, occlusion and final lighting execute the **unchanged production SPIR-V** on CPU Vulkan.
  This is not a rewritten CPU lighting equation or a full-application GPU raster screenshot.
- Readbacks are 384 × 256, with card resolution 4, volume resolution 32, cell size 0.15 m, shadow cone slope
  0.06, sun radiance 1.8, sky ambient 0.005 and 32 cache updates. These settings remain fixed across cases.
  No denoising, image smoothing, exposure adjustment or retouching was applied to the retained pixels.
- Camera variants are front, left orbit, high view and a camera twenty times farther from the target with a
  correspondingly narrower field of view. Geometry cases scale the entire scene and camera by 0.01 and 100,
  or translate both by (+10,000, −25,000, +1,000) m and (+1,000,000, −1,000,000, +1,000,000) m.
- The reference case moves one instance 1.5 m **without recreating the stage**, refreshes production geometry,
  captures its first frame and settled state, then restores it. Camera variants are separately warmed views,
  not a continuous interactive flythrough or a real-time performance demonstration.

Source: `fa6ee1b78051eb6367880a29521b3ae01854451b`.
[CPU scene workflow 37231216029](https://github.com/unassignedinbox/Slate/actions/runs/37231216029)
completed all eight execution jobs with no Vulkan validation errors. This is an **execution pass**,
not an image-quality or scale-invariance pass.

### Independent assessment — FAIL overall

`AssessSceneReadbacks.py` checks matching shader/image hashes and compares 850 identical physical floor
probe locations, excluding footprints hidden by objects. Errors are RGB display units on the 0–255 scale.
The fixed acceptance thresholds are RMS ≤ 1 and maximum channel difference ≤ 5.

| Case                               | Probe RMS | Maximum difference | Invariance result |
|------------------------------------|-----------|--------------------|-------------------|
| Left orbit                         | 0.0443    | 1                  | Within tolerance  |
| High camera                        | 0.0443    | 1                  | Within tolerance  |
| Camera twenty times farther        | 0.0443    | 1                  | Within tolerance  |
| Scene scaled to 0.01                | 9.3287    | 80                 | FAIL              |
| Scene scaled to 100                 | 1.3071    | 17                 | FAIL              |
| Translation (+10k, −25k, +1k) m      | 1.3201    | 17                 | FAIL              |
| Translation (+1M, −1M, +1M) m       | 8.7709    | 94                 | FAIL              |

Restoring the moved instance reproduces the original entire RGB image exactly: RMS 0 and maximum 0.
This does not establish artifact-free lighting during continuous motion.
`AssessSceneReadbacks.py --enforce` returns failure for the four scale/translation cases; thresholds were
not relaxed to turn these into passes. `Assessment.json` explicitly records `overallInvariancePass: false`.

**Visual inspection also fails the broader quality claim:** GI-on renders retain obvious triangular,
patchy/banded indirect lighting on the floor and coloured walls. These artifacts are absent from the
corresponding GI-off lighting. C025's restricted receiver-plane correction is therefore **not a complete
fix for general scenes**, even though its earlier numerical checks and the execution regressions passed.

Fixed world-space ray offsets/tolerances and float world-coordinate precision remain relevant suspects
for scale/translation sensitivity. Finite sampling/card representation remains relevant to the spatial
GI artifacts. This phase records those observations, not a verified diagnosis or a new production fix.
Far/large cases may use the production exact-mesh fallback outside finite clipmaps; their output does not
prove voxel coverage at arbitrary distances. No finite set of captures proves "any scale or distance".

### Retained deliverables

`VisualProof/SdfScene/LightingAndMotion.png` shows GI off/on and live-instance motion.
`VisualProof/SdfScene/ScaleDistanceAndViews.png` shows all eight views/stresses.
Individual unaltered PNGs, logs and source/SPIR-V/image provenance are under `Captures/<case>/`.
`Assessment.json` and the repeatable assessor retain both passing and failing measurements.
The user has not retested these changes on their own GPU; GPU performance and full application parity
remain unverified. No editor layout, baking placement or production shader was altered in this phase.


## C027 — Diffuse quadrature decorrelation and incident-light reconstruction (2026-10-04)

Status: **verified reduction of coherent banding in the retained scene views**. The prior C026 images remain untouched in
`VisualProof/SdfScene/Captures`. Scale and large-world corrections are explicitly out of this phase's scope.

The primary gather previously used identical cosine/area quadrature at every pixel, projecting coherent
occluder/emitter contours. The new production pass gathers pixel-decorrelated incoming diffuse lighting
into an RGBA16F image (irradiance divided by π), with resolved shading normals in a second RGBA16F image. A 7×7 spatial reconstruction
rejects different instances, incompatible normals and off-plane neighbours before the final material
response. Direct lighting/shadows, textures, emission, specular and tone mapping are not filtered.
Secondary reflection/transmission hits still use world-space transport, never screen-cache substitutes.
Cache-card quadrature/history remains unchanged. The pixel sequence is static, not a cycling noise phase;
continuous-camera stability is not established by static screenshots.

Integration includes a compute-write→read barrier, two lifecycle-owned images (16 bytes/pixel combined),
explicit allocation dimensions and bounded frame extents, a 112-byte push block, indexed/fixed material
shaders and every shipping/proof shader build list. Historical baseline shader comparisons receive an
ABI-only trailing extent member; their original resolve ignores the new images and retains its equations.

Independent validation is requested for Reference/Orbit/High with identical scene/camera/material settings,
plus a 512-sample-per-technique unfiltered **production-shader quadrature reference**. The latter is a
numerical comparison, not a real-time renderer or ground truth for card-cache/transport accuracy. New raw
readbacks belong under `BandingAfter`; no retouching or post-capture image denoising is permitted.
Existing artifact, materials, transmission, texture, lifecycle and resize gates remain unchanged.

Local Vulkan compilation/execution is unavailable: package repository connections failed and the sandbox
has no Vulkan SDK/CPU ICD. Authorized GitHub Actions performs actual compilation and CPU Vulkan execution.
The actual readbacks and independent comparison results are recorded below; they have now been inspected.

### Dynamic deforming geometry: recommendation, not implemented

The existing SDF path calls `TraceDistanceMesh`: it contains **software triangle ray tracing** and must not
be presented as ray-free. Unreal software Lumen's SDF visibility is also not an arbitrary-deformation
solution. A genuinely raster-only alternative is full-geometry radiance/depth probes: rasterize the actual
current/skinned/deformed triangles into local cubemaps, integrate diffuse irradiance, preserve directional
depth/visibility, and ping-pong irradiance for additional bounces. Moving geometry both receives and
contributes, including off-camera contributors. No simplified mesh, triangle ray query or SDF marching is
needed for this proposed path; direct visibility must use rasterized shadow maps too.

Probe visibility/interpolation remains a finite-resolution approximation, not exact per-receiver triangle
visibility. Six views per probe, dense mesh throughput, update latency and leakage around thin structures
must be measured. Dirty bounds invalidate old/new affected probes and their bounce history. Most geometry
being static helps scheduling, but does not make arbitrary deformations free. Stock DDGI is ray-traced;
only its cache/visibility ideas transfer when capture is explicitly replaced by rasterization.

Sources:
- Epic, Lumen final-gather radiance-cache filtering and importance sampling:
  https://www.unrealengine.com/en-US/tech-blog/unreal-engine-5-goes-all-in-on-dynamic-global-illumination-with-lumen
- Epic, software/hardware visibility and limitations:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Rasterized cubemap GI in dynamic diffuse environments (mechanism, not modern performance evidence):
  https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk02cmdsigi.pdf
- DDGI directional irradiance/distance-moment visibility (original capture uses rays):
  https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf


### C027 executed evidence and remaining limits

Production implementation source: `259c46c67e28b33ad621070eb2d1be3d2482d4d8`.
Scene run: https://github.com/unassignedinbox/Slate/actions/runs/37233160992
Existing regression run: https://github.com/unassignedinbox/Slate/actions/runs/37233160945

All four scene jobs succeeded: Reference (including same-stage movement/restoration), Orbit, High and the
512+512 unfiltered quadrature comparison. Inspection of all three before/after views shows the repeated
floor rings and angular wall banding removed at the captured resolution. The new outputs retain GI; the
Reference GI-on/off RMS is 16.0892, rather than zero. No original scale/translation results were overwritten.

`AssessBandingReadbacks.py --enforce` passes with unchanged-direct-lighting, exact restoration and less
than half the previous RMS error in each comparison region. Display RGB units are 0–255:

| Region / property             | Before     | After      |
|-------------------------------|------------|------------|
| Whole-image RMS vs 512+512     | 1.571068   | 0.653368   |
| GI-affected-pixel RMS          | 2.756555   | 1.146281   |
| Foreground-floor RMS          | 1.770556   | 0.522515   |
| Foreground-floor maximum      | 11         | 3          |
| Whole-image maximum           | 45         | 47         |
| GI-off change, all three views | —          | RMS/max 0  |
| Object restoration difference | —          | RMS/max 0  |

The 8,423-pixel floor region is a fixed projected physical rectangle, X within ±4.8 m and
Y between −3.8 and −1.05 m. It contains no foreground objects; the script does not compute lighting.
All images are hashed against execution provenance. The dense reference shares the same production
transport/card cache and therefore does **not** independently validate those approximations.
The whole-image maximum did not improve: isolated edge/contact errors remain (maximum 47), including
reconstruction bias near sharp indirect-visibility changes. Do not advertise the output as artifact-free.
Static sampling does not establish continuous-camera temporal stability. GPU frame time is unmeasured.

Existing ArtifactBaseline, ArtifactAfter and complete Regression jobs all pass. Regression includes
actual descriptor-indexed material/texture sampling, textured card bounce, static convergence, instance
movement and history reset, geometry/material changes, reflections, thin/solid transmission, Beer
attenuation, resize, shipping-resolution card seams, and synchronization/resource destruction validation.
Vulkan validation reports **zero errors**. No existing threshold was relaxed.

Restricted analytical/artifact metrics also improve: matte symmetry RMS 4.23736 → 0.637561,
single-bounce reference RMS 2.67571 → 0.543786, subdivision RMS 2.76445 → 0.58333.
Camera-shadow RMS and zero-specular reflection delta remain zero. These narrower checks supplement,
not replace, the independent scene images.

Main deliverable: `VisualProof/SdfScene/BandingComparison.png` (inspected and opened in the side viewer).
`BandingAssessment.json` retains every measured value, including the worse maximum. Raw captures and
execution/SPIR-V provenance are under `BandingAfter/<case>`. No image denoising, resizing, retouching or
exposure manipulation was performed after capture.

For the proposed raster-probe dynamic path, static and dynamic visibility must be composed in the **same**
probe captures, not added as two independent ambient terms. Reuse static geometry/depth captures where
valid, restore them before drawing each new dynamic pose, and refresh lighting when dynamic shadows or
bounce illumination change. This prevents stale moving-object depth and double-counted static lighting.
The probes are a lighting representation, not substitute low-polygon meshes. The proposal remains
unimplemented; supporting actual deformed vertex streams and budgeting probe updates is a separate task.

Native Project Zero editor/stack checks, browser checks and Drive checks passed in the same workflow.
At evidence checkpoint time the full Windows host/project packaging job and SolidArc checks are still
running. This entry does not claim those entire jobs passed; consult the linked workflow for later status.
The post-execution edits only add the trailing-member offset assertion, source-header/indentation cleanup,
the independent readback assessor and documentation; transport/lighting equations remain those executed.

## C028 — Interactive full-geometry raster-probe GI in HTML (2026-10-04)

User accepted the native banding repair and requested an HTML demonstration of the proposed dynamic GI.
Delivered `Frontier/Experimental/RadianceProjection/index.html` with `ProbeIntegrator.js`, using the existing
vendored Three.js r160, OrbitControls and native DM Sans font. No CDN dependencies, native code changes,
new low-polygon visibility mesh, ray queries, SDF marching or fabricated ambient GI are involved.

### Implemented mechanism

- Actual 3,006-triangle scene; the deforming ribbon has 841 vertices and 1,568 triangles. Its position buffer
  is deformed and its normals recomputed. Primary rendering, probe captures and the sun shadow map consume
  that same geometry. No alternate proxy representation is constructed.
- A 4×3×4 world-space grid captures six 32×32 raster views per updated probe. HDR RGB stores outgoing
  radiance and alpha stores radial distance; uncovered directions are black with a far distance of 20 m.
- 128 cosine-weighted hemisphere samples integrate each directional irradiance texel on the GPU. A 16×16
  octahedral tile per probe stores irradiance/π. A second atlas stores directional depth mean and mean square.
- Eight-neighbour interpolation combines trilinear weights, surface orientation and moment-based visibility.
  Double-buffered atlases avoid render-target feedback. Recursive captures sample the previous atlas, giving
  approximate additional diffuse bounces. Direct sunlight is separately shadow-mapped, with receiver-plane
  depth correction for PCF. Tonemapping happens only in presentation, never in the capture feedback.
- Every update rerasterizes the actual scene. Static-capture reuse, adaptive probe placement and dirty-bounds
  scheduling are **not** implemented in this first demonstration. Its purpose is to expose the working
  mechanism and trade-offs, not promise shipping performance or full native parity.

Controls: GI toggle, indirect-only/normals, motion pause, deformation amplitude, lateral displacement,
ribbon visibility/colour, emitter and sun strength, recursive bounce, depth visibility, probe markers,
1–12 probes/frame, freeze/resume updates and clear history. The lower view shows an actual selected probe's
six radiance/depth faces or the live irradiance atlas. Camera orbit/zoom/pan and mobile reflow are supported.
To see emission-driven bounce clearly, set **Direct sun to zero**. Freeze captures, move/deform the mesh,
then resume them to inspect stale versus refreshed indirect lighting. Pausing motion makes GI comparisons easier.

### Executed browser checks

`VisualProof/RadianceProjection/VerifyBrowser.cjs` runs Playwright against the served page and reads the
actual WebGL framebuffer. Retained screenshots and `Execution.json` are in its `Captures` subdirectory.
The local Chromium run used software graphics; reported frame intervals are RAF intervals, **not GPU timings**.
Normal Playwright Chromium works with the runner; the optional `PROBE_LAMBDA_CHROMIUM` route uses the
sandbox's scratch-installed Chromium package. Browser dependencies are not committed.

| Verification                                        | Result          |
|-----------------------------------------------------|-----------------|
| GI toggle, RGB RMS / maximum                         | 10.483 / 120    |
| Sun off: mean emissive GI on otherwise black receivers | 29.500 / 255    |
| Moved geometry: stale vs refreshed GI RMS / maximum   | 8.006 / 91      |
| Sun/emission off and history cleared: GI difference   | RMS 0 / max 0   |
| Actual ribbon vertex changes                        | Verified        |
| Frozen probe cursor                                 | Unchanged       |
| 390-pixel mobile layout                              | No overflow     |
| Shader/browser errors; WebGL readback errors           | Zero            |

The screenshot cases include final/direct-only, emitter-only/GI-off, moved-frozen/moved-updated,
radial depth, irradiance atlas, no-light and mobile layout. Raw screenshots are not denoised or retouched.
The source SHA-256 digests in the execution report identify the HTML and renderer used for these checks.

Initial inspection exposed uninitialized cubemap camera orientations (six repeated views); explicitly
initializing Three's cube coordinate system corrected them before validation. Direct-shadow PCF initially
showed self-shadow striping; receiver-plane depth correction removed it before the retained checks.

Limitations remain visible: coarse probe/angular resolution, possible thin-surface leakage, mixed-age
captures during motion, interpolation bias and finite grid coverage. Freeze/low-budget controls intentionally
expose latency. No glossy transport, arbitrary imported/skinned asset workflow, probe relocation or native
integration is claimed. Existing C027 native SDF correction and all C026 scale failure evidence are untouched.

## C029 — Change-prioritized raster-probe refresh and static cache hold (2026-10-04)

User confirmed the HTML GI works and requested reduced latency and clarification of camera-local capture.
Updated the same `Experimental/RadianceProjection` demo, without changing the native SDF renderer.

### Implemented changes

- `ProbeScheduler.js` orders pending work by fresh scene revision, changed old/new ribbon bounds, projected
  floor-shadow bounds, camera relevance and waiting age. Every scene change conservatively invalidates all
  probes: distant/off-camera contributors are not silently excluded. This is prioritization, not an exact
  dependency solver or a claim that distant GI cannot be affected.
- The regular budget remains four probes per frame. A selectable **Round-robin baseline** retains the old
  continuous update behavior for comparison. No geometric meshes are simplified, replaced or repositioned.
- Up to four refresh captures per probe provide bounded recursive settling (one when recursive bounce is
  disabled). After settling and with no scene changes, the irradiance/depth caches are held; no atlas copies
  or probe raster captures are performed. This is a bounded approximation, not a general convergence proof.
- The sun shadow map is also reused until geometry/visibility changes. The baseline mode deliberately
  retains its old continuous shadow redraw. Main-view geometry still renders normally in both modes.
- Invisible ribbon animation no longer invalidates lighting. Showing it again evaluates its current pose
  and invalidates the relevant work. Camera motion alone reorders pending work but does not invalidate the
  world-space field. Changing the diagnostic probe can explicitly refresh an otherwise-idle distant probe.
- Optional **Interaction burst** allows up to 3× the base budget, capped at 12 probes/frame, for two frames
  after an edit. Continuous dragging can keep requesting bursts. This spends additional rendering work;
  it is explicitly disabled in the equal-budget comparison.
- Inspector controls now expose scheduling, burst cost, pending probes, actual captures/views this frame
  and first-probe response. First-probe response is not full-volume convergence. Last-capture age can be
  large for an unchanged valid cache and is not itself edit latency.

### Executed comparison — real WebGL, equal budget

`VerifyLatency.cjs` changed the ribbon from X=0 to X=1.3 with motion paused, compared the same eight highest
priority probes, and recorded actual capture selections/revisions. Both modes used four probes/frame and
had bursts disabled. Results from the final run:

| Response for those eight probes | Prioritized | Round-robin |
|---------------------------------|-------------|-------------|
| First refreshed probe           | 1 frame     | 2 frames    |
| Mean refresh arrival            | 1.5 frames  | 7 frames    |
| Last refreshed probe            | 2 frames    | 12 frames   |

This is a specific edit/starting-cursor comparison, not a universal 6× frame-rate improvement. Prioritizing
one area can delay others. Aging prevents perpetual starvation: the deterministic 240-frame continuous-
change check served every probe, with a maximum observed gap of 24 frames at budget four.

Static hold submitted **zero probe captures and zero shadow redraws**, leaving 10 main-view/inspector draw
calls in this fixture. Four additional explicitly requested settling captures changed the held RGB image
by RMS **0.019005**, maximum **1** (0–255 units). That supports the chosen budget in this scene only.
GI-on/off still differed by RMS **10.7771**; the optimization did not turn off indirect light.

Camera movement without scene changes preserved the field revision and submitted no probe captures.
Frozen updates preserved their cursor; a selected distant probe still refreshed; hidden animation settled
without perpetual invalidation; the 12-probe burst cap and mobile overflow check passed. No browser/shader
errors occurred. Readbacks reported no WebGL errors. All application source hashes match both reports.

Re-ran the previous lighting correctness suite against the optimized implementation:
- GI-on/off RMS **10.4037**, maximum 120.
- With sun off, otherwise-black receivers still received captured emissive lighting (mean **28.6091**).
- Frozen versus refreshed lighting after moving geometry differed by RMS **8.07356**.
- With sun/emission off and cleared history, GI-on/off remained exactly identical: RMS/max **0**.
- Actual vertex deformation, frozen captures, depth/atlas inspection and mobile layout passed.

Retained evidence: `VisualProof/RadianceProjection/LatencyCaptures/` and `RegressionAfter/` contain the
unretouched screenshots, source hashes and execution reports. The original C028 `Captures/` is preserved.
`VerifyScheduling.mjs` checks ordering/budgets/aging without graphics; `VerifyLatency.cjs` exercises actual
WebGL. The latter uses normal Playwright Chromium, with an optional `PROBE_LAMBDA_CHROMIUM=1` scratch
package route for this sandbox. Software-browser execution is not target-GPU timing evidence.

### Camera distance and packing — what this does and does not implement

The probes are anchored to **world positions**, not the main camera. The demo still has one fixed room-sized
volume; it does not implement large-world streaming/cascades. A larger renderer can allocate dense volumes
near important actors/camera-visible receivers and coarser or cached coverage farther away. Off-screen
emitters, bounce surfaces and blockers must remain in the relevant probe captures.

Pack probe images and draw commands, not the objects' world positions into an artificial lighting scene.
Each scheduled capture here still rasterizes its contributing full meshes, using the probe camera's normal
frustum culling. Static geometry G-buffer/depth reuse with dynamic-geometry compositing would be a further
optimization; it is **not** implemented by this scheduling change. Lighting/shadow changes would still
require reshading those cached static surfaces. Thin-surface leakage, finite probe resolution, mixed-age
captures and higher-bounce latency remain; this iteration reduces latency rather than claiming to erase it.

## 2026-10-05 — C030: polygon-only cliff construction workspace

### Scope and inspected source

The user switched from the completed GI latency work to the cliff generator at
<https://github.com/darkenigmainbox/Frontier/tree/arena%2F01a1089a-frontier>.
Inspected source revision: `b0a0cc578b33bb73b661ae248deacf6a84eb57fb`.
The explicit exclusion of the SDF stage governs this implementation.

Source inspection found three full-width wedge masses, global plane cuts, whole-edge bevel-style chips,
a Flask/API-dependent viewer, and a failure path that silently substituted previously exported stage OBJs.
The Python SDF meshing/erosion path is **not** ported, invoked, or approximated with a hidden distance field.
The other repository is unchanged; this is a new static-browser implementation in this session's Slate branch.

### Delivered files and construction mechanisms

`Frontier/Experimental/CliffSequence/index.html` is the entry point. It uses the repository's existing Three.js,
OrbitControls and DM Sans assets. There is no Python backend, generated-file endpoint, third-party runtime CDN,
or precomputed OBJ fallback. The new module does not change the native engine, Fluid, SolidArc, or GI demo.

- `CliffSpecification.js`: three authored landforms—headland, stepped escarpment and amphitheatre—with crown,
  bay, buttress and cross-section profiles. Width, height, depth, relief and retreat expose geometric controls.
  Seed chooses discrete bedding, joint, spall and fissure catalogue entries/occurrences, not vertex offsets.
  The base silhouette is controlled by the chosen profile and dimensions, not by a spatial noise function.
- `PolyhedronSolver.js`: build a continuous profile loft through convex construction cells; clip cells against
  geological planes; cancel internal interfaces and stitch coplanar contours before meshing. Subdivide shared
  constraints consistently and triangulate planar faces, including incision holes, with constrained Delaunay
  triangulation. This is polygon meshing, not Voronoi fracture/noise generation.
- `FractureSequence.js`: five cached stage results:
  1. Continuous macro mass with concave bays and projecting buttresses.
  2. Coherently dipping beds using authored thickness sequences and actual joint apertures.
  3. Staggered joints restricted to the front volume, terminating at a finite rear plane. Per-block supporting
     face cuts create localized recesses; rear strata are not split by the vertical joint family.
  4. Bounded edge spalls. Wedge and flake plans replace a local patch on two adjoining faces with four or six
     real fracture facets. At least ten percent of the original edge length remains beyond each end of a cut.
     A concave spall cavity is not an infinite bevel plane applied to an entire edge.
  5. Finite, kinked, tapered-mouth polygon V-grooves on exposed faces, with closed bottoms and bounded depths.
- `TriangleSolver.js`: local link-condition edge collapses, diagonal flips and cap repairs. Each cleanup
  operation uses a 0.12 m local tolerance and rejects triangle reversal; this is not a global error bound.
  Graded constraint refinement concentrates triangles around small cuts without changing the planar surface.
  Spalls/fissures that increase their parent body's below-5-degree triangle count are rolled back and counted.
- `GenerationQueue.js`: a cancellable module worker. Revisions reject obsolete messages. Structural defects,
  non-finite/non-positive body volumes, and invalid recipes produce explicit failures, not old geometry.
  Starting a rebuild discards the previous result and render meshes; OBJ export is disabled until completion.
- `WorkspacePanel.js` / `.css`: native-style document rail, outliner, persistent Editor Camera entry, inspector,
  stage navigation, draggable parameter sliders, clay/wireframe/cut-surface views, lighting controls, orbit/pan,
  double-click rock inspection, isolation, exploded inspection, and a camera-plane scale indicator.
  Exploded/isolated display transforms do not modify exported geometry.
- `PlanarSolver/cdt2d.js`: locally bundled `cdt2d` 1.0.0 and exact-predicate dependencies; their MIT notices and
  versions are retained together in `PlanarSolver/LICENSE.txt`. No dependency uses spatial noise to make shape.

There are no normal/displacement maps, procedural noise textures, tessellation-based silhouette changes,
or textured materials concealing the geometry. Clay uses the actual triangle normals. The cut-surface colour
view is an explicitly selected diagnostic, not the default material.

### Geometry verification

`VisualProof/CliffSequence/VerifyGeometry.mjs` executes 13 recipes / 65 stages, including three landforms,
multiple seeds, opposite dimensional extremes, maximum relief/retreat, zero damage, dense damage, and fine cracks.
All tested stages passed finite/indexed triangle checks, closed edge incidence, consistent edge winding,
vertex-link manifoldness, duplicate/degenerate triangle checks, positive body volumes and bounded rear joints.
The spall check also verifies that both ends of each original edge survive. Replaying the default produced
identical geometry and feature records. No-damage stages preserve their preceding triangle topology.

Default recipe: headland, seed 42, 32 × 18 × 12 m:

| Stage             | Mesh objects | Vertices | Triangles | Spalls | Fissures | Triangles below 5° |
| ----------------- | ------------ | -------- | --------- | ------ | -------- | ------------------ |
| Macro mass        | 1            | 273      | 542       | 0      | 0        | 0                  |
| Bedding           | 7            | 4,501    | 8,966     | 0      | 0        | 0                  |
| Bounded joints    | 56           | 8,313    | 16,390    | 0      | 0        | 0                  |
| Edge spalls       | 56           | 12,155   | 24,074    | 141    | 0        | 0                  |
| Surface fissures  | 56           | 14,575   | 28,914    | 141    | 25       | 0                  |

All five default stages have zero open/nonmanifold edges, nonmanifold vertices, duplicate/degenerate triangles
and edge-winding errors. Final minimum triangle angle: **5.32127°**. Two spall attempts and two fissure attempts
were rejected by the quality check rather than silently introducing narrower triangles.

`VerifyIntersections.cjs` independently checks all five default stages using a BVH broad phase and
separating-axis triangle tests in double precision. It found **zero self-intersections and zero inter-body
surface intersections**. Coplanar pairs are included. Shared-vertex pairs are contracted toward their
centroids by 1e-4 to exclude legitimate boundary contact; the projection comparison tolerance is 1e-8 m.
This numerical fixture check is not a proof for every possible parameter combination or sub-tolerance contact.
The verifier includes known intersecting and separated triangle fixtures. Its npm packages are verification
requirements only: `three` 0.160.0 and `three-mesh-bvh` 0.8.3; neither replaces the shipped repository assets.

### Remaining limits and use

- This is an authored geological modelling system, not a fracture-mechanics or physical erosion simulation.
  It deliberately leaves SDF erosion/remeshing out. Joint apertures, finite face recesses, spalls and incisions
  are polygon geometry; no general weathering solver is claimed.
- Upper strata can contain disconnected solid components within one named mesh object. Counts above are mesh
  objects, not a claim that every object contains exactly one connected rock.
- Twelve of the thirteen tested recipes have no triangles below 5 degrees at any stage. The deliberately
  extreme 48 × 10 × 8 m, maximum-relief/retreat, ten-bed recipe retains five such triangles in its final mesh
  (minimum angle 3.98062 degrees). The UI reports these warnings. It does not assert universal sliver-free output.
- Triangulation span controls tessellation density, not displacement. Fine cracks and quality refinement can
  make later stages much denser than the macro mesh. Geometry generation is CPU work in a worker; no target-GPU
  performance claim follows from headless browser verification.
- Open the hosted HTML link, or serve the repository root over HTTP and open
  `/Frontier/Experimental/CliffSequence/index.html`. ES modules/workers require HTTP; opening a bare file URL
  is not the supported launch route. No npm install or Flask server is needed to use the delivered app.
- Stage buttons reuse completed results. Changing a parameter rebuilds after the edit is committed; a new edit
  cancels the old worker. Double-click a rock, scroll closer, and switch stages 3/4 to compare a finite spall.
  `F` frames the selected rock, Escape clears selection, and Frame restores the whole cliff.
- Export OBJ writes the chosen stage as indexed triangles in metres, with object names and flat shading.
  Save/Load recipe uses a versioned JSON schema. Neither export depends on a server-side generated filename.

### Executed browser checks and retained evidence

The complete final-source run of `VerifyBrowser.cjs` passed in Chromium **133.0.6943.0** with software WebGL 2.
The execution report records zero browser errors and a zero WebGL error readback. It covers:

- All five stage buttons and actual image changes between mass, joints, spalls and fissures.
- Clay with no colour/normal/displacement textures, full triangle wireframe and explicit cut-surface colouring.
- Actual OBJ download: **14,575 vertices / 28,914 triangle faces**. Every face has three valid indices.
  Exploded inspection does not change the OBJ. The downloaded file exactly matches the generated export text.
- Double-click-equivalent selected-rock framing, selection/isolation, actual mouse orbit, and full-cliff framing.
- Dirty settings disabling export, superseded workers being cancelled, and the newest recipe winning.
- Deliberately invalid generation clearing the result/render meshes and showing a failure, without fallback.
- Recipe download/import, repeated preset changes, and fresh rendered captures of both alternative landforms.
- A 390 × 844 layout with no horizontal overflow and functioning stage buttons.
- Deliberate WebGL context loss disabling export/rebuild and requiring reload instead of exposing a stale mesh.

`VisualProof/CliffSequence/Captures/` retains the final unretouched browser screenshots, raw alternative-preset
viewport readbacks, default JSON recipe, geometry/intersection reports and browser report with matching source
hashes. In particular, `LocalSpallBefore.png` and `LocalSpallAfter.png` use the same camera; `SurfaceFissure.png`
shows a genuine recessed groove; `TriangleWireframe.png` exposes the tessellation rather than hiding it.
Generated inspection OBJs, npm packages, browser binaries and intermediate captures remain in ignored scratch.

Browser limitation: the sandbox's Chromium 153 / SwiftShader build stalled during some preset readbacks with
ground or shadows present. The same application, including shadows, completed the full suite in Chromium 133.
This does not establish that the Chromium 153 software-rendering path is fixed, or certify target-GPU/browser
performance. The verifier can use normal Playwright Chromium, or an explicitly selected sandbox executable;
its optional compositor resize/readback synchronization is test-side only and does not alter the mesh.

## C031 — All-sided cliff profiles, flexed cuts and selected-stage rebuilding

Date: **2026-10-05**. This revises C030 in response to the confirmed-working app and the three supplied captures.
C030's original captures remain in `VisualProof/CliffSequence/Captures/`; the new evidence is separate.

### Geometry changes

- Retained the authored front massing. Added independent rear cross-sections and height-dependent end profiles;
  the back is no longer a constant-depth plane. The convex construction cells still share exact interfaces.
- Replaced globally planar bedding with continuous, piecewise-planar flexures and step-overs. Boundary families
  use different authored traces and oblique axes. Aperture pinches along intact portions and opens at ramps.
  `Fracture flexure` controls their amplitude. These are actual cutting surfaces, not a displacement function.
- Front and rear exposures receive finite, kinked joints. End portions are separated from a retained interior
  core; their narrow connecting portions are not needlessly subdivided again along their depth. All exposed
  sides can receive recessed faces, localized spalls and finite surface fissures.
- Joint placement avoids nearby construction corners where a bounded alternative exists. Numerical corner
  welding now searches adjacent tolerance buckets rather than assuming rounded coordinate keys are sufficient.
- Edge-collapse and diagonal-flip repairs reject new triangle intersections. Damage rollback now also rejects
  structural defects, not just additional narrow triangles. This caught and removed a faulty local spall in
  the fine-crack/coarse-triangulation recipe during verification.
- Still no SDF, noise, stochastic vertex displacement, texture-based shape or whole-edge bevel substitute.
  Triangle-only export remains independent of display colour, camera, selection and exploded inspection.

### Selected-stage workflow

The app starts at **stage 1**. Select a stage, edit its controls, then press **Rebuild through 0N**.
Moving a slider or pressing New seed marks the parameters for rebuilding; it no longer launches all five stages.

- A persistent worker retains geometry checkpoints and completed triangle results.
- Each parameter declares its earliest dependent stage. Only that stage and its downstream results are invalidated.
- A request stops at the selected stage. Valid upstream stages are reused, not recalculated.
- Per-stage catalogue streams make stepped generation identical to a one-shot build of the same recipe.
- Selecting an unbuilt stage shows the last valid upstream input, explicitly labelled as input, with OBJ disabled.
- Later-stage rebuilds retain the current camera. Cancelling an in-flight job terminates that worker; this also
  discards its checkpoints, but the replacement job still stops at the selected stage.
- Generation failure and WebGL context loss still clear invalid output and disable export rather than substitute
  an earlier or prebaked result.

### Executed verification

`VerifyGeometry.mjs` passed **14 recipes / 70 stages**, including the supplied Amphitheatre parameters
(seed 42, 32 × 18 × 12 m, five beds, −5° dip). Deterministic replay passed. Every checked mesh had finite indexed
triangles, positive volume, and zero open/nonmanifold edges, nonmanifold vertices, duplicate faces, degenerate
triangles or inconsistent edge winding.

Default recipe, final implementation:

| Stage | Bodies | Vertices | Triangles | Minimum angle | Spalls | Fissures |
|-------|--------|----------|-----------|---------------|--------|----------|
| 1     | 1      | 347      | 690       | 11.4367°      | 0      | 0        |
| 2     | 7      | 6,337    | 12,638    | 5.1255°       | 0      | 0        |
| 3     | 117    | 16,751   | 33,010    | 5.0188°       | 0      | 0        |
| 4     | 117    | 26,976   | 53,460    | 5.0188°       | 353    | 0        |
| 5     | 117    | 30,575   | 60,658    | 5.0188°       | 353    | 36       |

The default has **zero triangles below 5°** in every stage. Ten proposed spalls were rejected; no default fissures
were rejected. A named body can contain more than one disconnected closed component, as in C030.

Independent Float64 separating-axis checks passed all five stages of **both the default and user Amphitheatre
recipes**, with zero self-intersections and zero cross-body triangle intersections. The verifier retains C030's
1e−8 m comparison tolerance and 1e−4 relative contraction of shared-vertex pairs to exclude legitimate contact.
This is not an exhaustive intersection proof for every possible parameter combination.

Quality limits remain visible rather than hidden: the user Amphitheatre ends with **one** sub-5° triangle
(minimum 3.48535°, 44,474 triangles). WideShallow ends with **eight** (minimum 1.00005°, 77,574 triangles).
Some other nondefault recipes also retain narrow-angle warnings. Local repair is bounded to 0.12 m per operation,
not a global cumulative shape-error guarantee. This remains authored geological geometry, not fracture mechanics.

`VerifyCheckpoints.mjs` passed selected-stage stopping, all parameter dependency declarations, unchanged upstream
geometry, downstream invalidation and byte-identical one-shot/stepped output. A Node run measured approximately
0.24 / 2.61 / 5.24 / 7.47 / 8.80 seconds for the individual default stages; these are sandbox observations, not
hardware performance promises. A stage-1 rebuild does not pay the cost of stages 2–5.

`VerifyWorkflow.cjs` passed in Chromium **133.0.6943.0 / software WebGL 2**, with zero browser errors and zero GL
error readback. It exercised manual edits, persistent-worker reuse, all five individual rebuilds, explicit unbuilt
inputs, late-stage-only edits, cancellation, actual OBJ download, explode-independent export, failure clearing,
390 × 844 layout, context loss, and fresh renders of the user's Amphitheatre. Thirteen unretouched screenshots
include the rear mass, curved bedding traces, fractured rear, and same-camera spall before/after views.

Evidence: `VisualProof/CliffSequence/RevisionCaptures/` contains `Geometry.json`, `Checkpoints.json`,
`Intersections.json`, `UserIntersections.json`, `Workflow.json`, and the screenshots. `Workflow.json` records the
runtime source hashes. Generated full-mesh JSON and temporary OBJs remain in ignored scratch. C030's Chromium
153 software-rendering caveat is not claimed fixed by this revision.

## C032 — Separate three-dimensional radiance-cascades experiment

Date: **2026-10-05**. New static entry: `Frontier/Experimental/RadianceSequence/index.html`.
The existing `RadianceProjection` raster-GI demo and all native GI code remain unchanged.

### Construction and scope

- `SceneSpecification.js` owns the scene's shared triangle coordinates, materials, receiver charts and cascade
  dimensions. The displayed and traced coordinates are the same Float32 values, verified component by component.
- `CascadeSequence.js` owns the GPU radiance intervals, merge targets, diffuse transport iterations and live
  receiver-lighting targets. `WorkspacePanel.js` exposes scene controls and cascade inspection in the existing
  Project Zero HTML theme, reusing the shared Three/OrbitControls files and existing font/CSS resources.
- Four actual **3D spatial lattices**, each with a **full-sphere** cubemap directional discretization:

| Cascade | Spatial samples | Directions/sample | Interval in metres | Traced intervals |
|---------|-----------------|-------------------|--------------------|------------------|
| C0      | 16 × 8 × 16     | 96                | 0–1.25             | 196,608          |
| C1      | 8 × 4 × 8       | 384               | 1.25–3.75          | 98,304           |
| C2      | 4 × 2 × 4       | 1,536             | 3.75–8.75          | 49,152           |
| C3      | 2 × 1 × 2       | 6,144             | 8.75–18.75         | 24,576           |

Spatial sample count decreases eightfold, angular sample count increases fourfold, and interval length doubles.
There are **368,640 traced intervals per diffuse iteration**. This is not a 2D field extruded into a 3D viewer,
ordinary irradiance probes renamed as cascades, or a relabelling of the earlier raster-GI implementation.

Each RGBA interval records RGB radiance and scalar transmittance. Far-to-near merging uses eight spatial samples
and four finer angular cells, weighted by their cubemap solid angles:

```text
L = Lnear + Tnear × Lfar
T = Tnear × Tfar
```

The first diffuse iteration traces emission only. Subsequent iterations add reflected radiance from the previous
solution at triangle hits. The default four iterations therefore include direct area lighting and three reflected
transport updates. No added ambient term or precomputed lighting scene is substituted.

This experiment uses **software triangle rays in WebGL fragment shaders**, not hardware RT and not the prior
raster-only visibility method. AABBs are broad-phase rejection only; intersections use the actual scene triangles.
Opaque interval origins are classified against the planes of the shipped convex solids. The reference scene has
seven solids / 84 triangles, including a non-rigidly sheared occluder. This is not an arbitrary large-mesh backend.

The merged directional solution is integrated into six positive, axis-oriented cosine integrals. Receiver normals
use squared-component weights between those integrals. Live 16 × 16 lighting charts per face avoid retracing
visibility at every screen pixel. They cache illumination only, never simplified geometry, and are cleared and
recomputed on scene edits. Optional exact receiver-to-sample triangle visibility reduces local interpolation leaks.

The UI exposes emitter position/power, occluder position/deformation, diffuse iteration count, direct/full/bounce-only
comparison, each cascade's spatial layer, selected X/Y/Z sample, local radiance, merged radiance and transmittance.
Orbiting the camera does not trigger or change transport. Shader/capability failure is explicit rather than hidden
behind a prebaked scene.

### Executed numerical and browser verification

`VisualProof/RadianceSequence/VerifyTransport.cjs` passed in Chromium **133.0.6943.0 / SwiftShader WebGL 2**:

- All retained interval, merged, cosine-integral and receiver-lighting texels were finite and nonnegative.
  Transmittance stayed within floating-point tolerance of [0,1], and passive radiance stayed within the emitter bound.
- Independent CPU triangle intersections matched **4,096 sampled GPU intervals** in a moved-emitter, deformed-occluder
  scene. Maximum component error: **4.19617e−7**; no mismatches above tolerance.
- Independent CPU spatial/angular interpolation and merging matched **4,096 GPU outputs**, including 950 opaque
  near intervals. Maximum component error: **1.70186e−5**.
- GPU cubemap quadrature was positive at face resolutions 4, 8, 16 and 32. Each full-sphere sum matched **4π** with
  absolute error below **8.64e−7**. This check caught a bad two-argument-atan central-cell result during development;
  the final shader uses unary atan with a strictly positive denominator, not clamping of bad weights.
- Broad-phase bounds include 1e−5 m numerical slack; triangle barycentric edge comparisons use 1e−6 tolerance.
  These prevent precision-dependent disagreement at grazing outer triangle edges and are included in the reference.
- The raster vertex attributes and traced triangle upload were identical. Moving the emitter changed transport;
  changing top shear changed the actual triangle coordinates and completed a new lighting revision.
- Full lighting included positive reflected contribution over direct-only lighting, with no negative differences.
  Setting emission to zero produced exactly zero lighting after recomputation: no hidden ambient or stale source.
- Camera movement left the lighting target bit-identical. GL error was zero, browser errors were empty, and the
  390 × 844 layout had no horizontal overflow.

`VisualProof/RadianceSequence/Captures/` contains ten actual browser screenshots and `Transport.json`, including
source hashes, per-cascade numeric bounds, interval/merge errors, GPU quadrature checks and the executed sequence.
The captures show full/direct/bounce-only lighting, a coarser 3D layer, interval visibility, rear view, moved emitter,
deformed occluder, emission-off and mobile views. No generated image or offline render is used as the demo.

### Limits and research basis

This is a small inspectable experiment, not a production-GI or GPU-speed claim. Coarse spatial/angular interpolation
can leak light or blur shadows, tiny emitters can be undersampled, receiver-direction compression is approximate,
and multi-bounce updates arrive incrementally. Range is finite at 18.75 m; the exterior environment is black.
The displayed solve duration is scheduled browser latency, not a GPU timestamp or a hardware FPS benchmark.

The construction follows the radiance-interval and spatial/angular merge discussion in Osborne & Sannikov,
[Radiance cascades: a novel high-resolution formal solution][RCFormalSource], particularly sections 2.2–2.3;
[radiance.wiki][RCResources] provides the broader graphics references. The hierarchy is adapted to three spatial
axes and two angular axes here, rather than copying a 2D scaling rule.

[RCFormalSource]: https://eprints.gla.ac.uk/343746/1/343746.pdf
[RCResources]: https://radiance.wiki/

## C033 — Seeded cliff landforms and non-bedded primary fractures

Date: **2026-10-05**. Continues the cliff prototype from C031; the radiance-cascades experiment, earlier raster GI,
other prototypes and native code are unchanged. The user's new permission to use noise supersedes the earlier
no-noise restriction. **No SDF stage has been added.**

### Formation controls and workflow

- Stage 1 now owns the **formation seed**. Changing it varies actual crown elevations, promontory projections,
  station spacing and front/rear relief, rather than merely selecting later fracture/damage catalogues. Repeating
  the same recipe remains deterministic. `New seed` explicitly avoids returning the immediately previous seed.
- `ReliefProjection.js` implements layered gradient, ridged-gradient and cellular F2−F1 relief. These fields shape
  a coarse geological loft, not independent random vertices or a shader displacement texture. The authored profile
  continues to constrain the landform. Cellular relief is not Voronoi fracture partitioning.
- Variation strength and horizontal feature frequency are exposed. `None` or zero strength deliberately retains
  the authored mass; changing the formation seed with variation disabled does not change that mass.
- Preset selection applies dimensions, relief/retreat and noise settings, while retaining the chosen seeds and
  downstream fracture/damage parameters. Individual dimensions remain editable afterward.

| Landform preset | Width × height × depth | Noise family |
|-----------------|------------------------|--------------|
| Buttressed headland | 32 × 18 × 12 m | Ridged gradient |
| Stepped escarpment | 42 × 22 × 14 m | Layered gradient |
| Recessed amphitheatre | 36 × 22 × 16 m | Cellular ridges |
| Tall solitary spire | 16 × 44 × 14 m | Ridged gradient |
| Serrated needle ridge | 36 × 42 × 13 m | Ridged gradient |
| Broad broken escarpment | 64 × 22 × 16 m | Layered gradient |

The dimension limits are now 10–80 m wide, 10–56 m high and 8–24 m deep. Front/rear minimum thickness and an
orientation-preserving loft check constrain the noise. Tall loft cells choose the better valid diagonal, and tall
stage-1 masses use the requested triangulation span instead of the coarser overview span. Preset/dimension changes
reframe the camera with viewport padding; tall spires are no longer cropped by the toolbar.

### Primary fracture patterns

Stage 2 is now **Primary fractures**, with its own **fracture seed** and four choices:

- **Conjugate fractures:** persistent intersecting inclined families, plus a depth-oriented family.
- **Block-jointed rock:** approximately orthogonal orientation families.
- **Steep joint set:** predominantly steep fractures, without compulsory horizontal bed rows.
- **Dipping bedding:** retains the earlier authored, variable-aperture bed-cut method as an explicit option.

`RuptureSolver.js` partitions the largest remaining volume sequentially. Each cut uses six coherent relief knots
and matching polygon cut surfaces, and terminates at the already established boundaries of its selected fragment.
Small tip-shaving partitions are rejected. Cut count, family tilt, aperture and roughness remain editable. These
are geology-inspired construction presets, **not a stress solver or a claim of physically simulated rock failure**.

The approved stage-3 bounded-joint construction is retained. Local spalls and shallow fissures also remain;
damage avoids faces containing pre-existing planar voids, and a rejected fissure no longer clears those voids.

The initial view still builds **stage 1 only**. Editing is not an automatic full rebuild: select the desired stage
and press **Rebuild through 0N**. Valid earlier checkpoints are reused; later stages are not calculated. Formation
seed/noise changes invalidate stage 1 onward; fracture seed/pattern changes invalidate stage 2 onward. Recipes now
save as version 2. Loading version 1 selects the authored/no-noise mass and bedding, and maps its old seed into the
independent fracture seed.

### Geometry corrections made during verification

- Rough, non-horizontal cuts exposed planar contours with interior voids. Previously these could be emitted as
  separately filled faces with reversed boundary winding. `JoinCells` now preserves contour orientation, assigns
  void loops to their containing outline and shares edge segmentation across both outer and inner boundaries.
- Microscopic clipped corners exposed a scale problem in SAT-based repair guards: unnormalized edge-cross-normal
  axes could fall below the axis cutoff, while fixed contact tolerance could exceed the contracted boundary gap.
  Unit edge directions, coordinate-relative projections and a scale-aware tolerance now retain those axes. The
  independent BVH/SAT verifier has metre-, 100-micrometre- and 10-micrometre-scale overlap/separation fixtures,
  including shared-edge neighbours at nonzero world coordinates. This also permits legitimate endpoint cleanup
  of tiny spire slivers instead of conservatively retaining them. Repair distance remains bounded to 0.12 m per
  operation, not a claim of 0.12 m cumulative error.

### Executed verification and evidence

The final-source runs passed:

- **23 variation recipes / 62 stage outputs**, covering all six presets, seeds 42/913, all noise families, all four
  fracture patterns, and tall/wide dimension limits. Both seeds produce different mass meshes for every preset.
  As a separate control-point measurement, seeds 42→913 move corresponding front/rear loft samples by **3.14 m
  RMS** for the headland, and 2.73–4.57 m across the six presets; this is not a comparison of remeshed vertex indices.
- **16 regression recipes / 80 stage outputs**, including the legacy default and prior amphitheatre recipe,
  strong relief, narrow/deep/wide combinations, dense/no damage and fine fissures. All 142 tested stage outputs
  had finite triangle coordinates, positive body volumes, zero open/nonmanifold edges or vertices, zero winding
  errors, zero duplicate triangles and zero zero-area triangles. This does not claim exhaustive coverage of all
  possible parameter combinations.
- **34 stage outputs** received the independent BVH/SAT intersection check: all five stages of default, spire,
  needles, wide wall and headland seed 913; stages 1–3 for orthogonal, steep-joint and bedding alternatives.
  **Zero self intersections and zero cross-body intersections** were detected in these checks.
- Checkpoint replay matched one-shot geometry. Every parameter's earliest dependency, future-stage stopping,
  upstream reuse and late-edit invalidation passed.
- Two actual Chromium **133.0.6943.0 / SwiftShader** browser runs passed, with **31 screenshots**, empty error lists,
  GL error zero, runtime source hashes matching the delivered files, and no horizontal overflow at 390 × 844.
  They exercised preset sizing/framing, real seed variation/replay, all noise/fracture choices, independent fracture
  seeds, explicit rebuilds, worker reuse/cancellation, late edits, triangle OBJ download, unchanged export under
  exploded inspection, v2 recipe roundtrip, v1 import, visible angle warnings, failure handling and context loss.
  These are functional software-renderer checks, not hardware GPU performance claims.

The **new default** is clean at all five stages, including zero triangles below 5°:

| Stage | Closed mesh objects | Vertices | Triangles | Minimum angle |
|-------|---------------------|----------|-----------|---------------|
| 1 — mass | 1 | 360 | 716 | 10.325° |
| 2 — primary fractures | 8 | 4,790 | 9,544 | 5.107° |
| 3 — bounded joints | 54 | 10,354 | 20,472 | 5.088° |
| 4 — spalls | 54 | 16,325 | 32,414 | 5.001° |
| 5 — fissures | 54 | 18,473 | 36,710 | 5.001° |

Final default damage comprises **219 localized spalls and 20 fissures**, with three rejected spalls. Non-default
recipes are not universally free of narrow angles: the final spire preset retains two triangles below 5°
(minimum 3.963°), needles three (3.934°), and the wide wall three (3.199°). Other corpus recipes can retain more;
all counts remain visible in diagnostics and status. The prior authored amphitheatre's single warning is also
preserved and tested through version-1 import. No threshold was raised to hide these warnings.

Current evidence is in **`VisualProof/CliffSequence/FormationCaptures/`**: 17 preset/fracture screenshots and
`Presets.json`, 14 workflow screenshots plus `Workflow/Workflow.json`, `Variation.json`, `Geometry.json`,
`Checkpoints.json`, and the combined `Intersections.json`. The intersection report records each exact specification
and mesh digest, and the browser reports record source hashes. Full mesh JSON, temporary downloads and browser
packages remain ignored scratch. C031's `RevisionCaptures` remains historical evidence, not substituted for these
new runs. Verification entry points are `VerifyVariation.mjs`, `VerifyGeometry.mjs`, `VerifyCheckpoints.mjs`,
`VerifyIntersections.cjs`, `VerifyPresets.cjs` and `VerifyWorkflow.cjs` under `VisualProof/CliffSequence/`.

## C034 — Discrete grain-weathering study and Fluid-themed cliff authoring

**Date:** 2026-10-05. **Base:** C033 (`e13b040`). **Scope:** the experimental HTML cliff workspace only.
Native code, Fluid simulation, other experiments and the approved macro-geometry solvers are unchanged.

### Delivered controls and geometry

The cliff and material views now use the existing experimental Fluid theme: DM Sans, charcoal rounded panels,
trapezoid document tabs, pill sliders, outliner, inspector and document save/open actions. The Fluid theme is
imported directly; its source is not modified. The cliff retains its five-stage workflow and explicit
**rebuild only through the selected stage** behaviour.

The new **Grain weathering** tab is an isolated material study. On first activation it freezes an available
cliff triangle; double-clicking a cliff face selects an explicit source for **Sample cliff face**. Stored
attachments include body name, construction stage, triangle index, triangle coordinates and per-column
barycentrics. Requested patch size is clamped to the source triangle's interior clearance. If no ready cliff
source exists on first activation, the study is explicitly labelled as a standalone sample.

- Seeded hard-core sites and clipped Voronoi polygons form packed, irregular grain columns, not scattered spheres.
  Shallow fixed faceted crowns sit above two to eight buried grain layers and a permanent backing.
- Sandstone, crystalline-aggregate and weak-laminated coefficient presets control mineral fractions, initial
  cement strength, susceptibility and iron content. The laminated preset additionally weakens directional bands.
- A contact graph transports bounded water and dissolved tracer. Source orientation affects rain exposure and
  tangential gravity. Drying deposits tracer, susceptibility controls cement loss, and iron/moisture affect an
  illustrative oxidation state. Ledger residuals are displayed rather than assumed zero.
- Only exposed grains detach. Removal changes the occupied solid and exposes lower caps and sidewalls; the
  boundary is rebuilt as triangles. The pits remain visible in **Clay · geometry only**.
- Four editable mineral colours are mapped directly to vertex colours, with moisture, oxide and deposited-tracer
  modulation. Diagnostic views show moisture, bonds, oxidation/deposits, exposed layer and weak bands. Palette
  changes do not alter particle state or vertex positions. Rendering currently uses one roughness value.
- Run/pause, single-step, 20-cycle batches, dry cycles, particle picking and study save/open are operational.
  Weather changes are recorded as replay events; pending weather settings and the edited palette survive saving.
  Packing changes require rebuilding and cancel an in-flight worker. Invalid recipes cannot display stale geometry
  as a successful result. Studies are bounded to 2,000 illustrative cycles and 2 MB input files.

No material texture, sampled noise field, displacement, normal map, SDF or baking was added. The seeded noise
options previously approved for macro landforms remain separate and unchanged.

### Explicit first-prototype limits

This is **not yet natural-rock fidelity**. The deliberately coarse polygon grains remain visibly cellular.
It is a shallow **columnar** model, not arbitrary three-dimensional crystal growth or a calibrated chemical solver.
Crystalline aggregate is presently a material-coefficient preset, not distinct cleavage geometry. There are no
undercuts, falling debris, mechanically simulated flakes, per-grain BRDFs or subpixel grain aggregation. Source
attachments are frozen snapshots: weathering does **not** replace, remesh or erode the full cliff mesh. Cycles do
not correspond to geological time. These limitations are also stated in the inspector.

Triangulation is performed in grain units, followed by a roundoff-only weld and conversion back to metres.
Degenerate-area checks use the same grain units: the reported physical cutoff is `1e-10 × pitch²` square metres.
This avoids classifying valid micrometre-scale facets as zero-area using the macro-cliff's fixed metre cutoff.
The actual minimum physical triangle area and cutoff are both visible in Diagnostics. Minimum angles and the
**unchanged 5° narrow-triangle warning** remain visible, including a viewport warning badge.

The mesh is **not sliver-free**. Default fresh/weathered standalone snapshots retain 50/51 triangles below 5°,
with a minimum angle of 0.548°. Dense/fine cases are worse: the tested 6 cm, 36-by-36 case retains 298 warnings
and a 0.044° minimum angle after weathering. This remains an unresolved quality limitation, not a clean-mesh claim.

Boundary rebuilding runs in a worker, but is not realtime; default snapshots took several seconds in the sandbox.
Software-browser verification is not a hardware-GPU performance benchmark.

### Executed verification

`VisualProof/CliffSequence/VerifyGrains.mjs` passed **14 cases / 28 initial-and-weathered snapshots**, including:
all three presets; multiple seeds; dry, zero dissolution, zero oxidation and zero transport; sizes 0.06–1.2 m;
resolutions 12–36; layers 2–8; complete removal down to the backing; upward-facing attachment; and a source-clamped
small triangle. Every tested boundary had finite coordinates, positive volume, no open/nonmanifold edges or
vertices, no winding errors, no duplicate triangles and no triangles below the declared grain-relative area cutoff.

The checks also assert bounded particle water/bonds/oxide, monotone grain removal, unchanged packed coordinates,
conserved water/tracer ledgers, analytical occupied volume versus an independent physical-coordinate mesh-volume
sum, deterministic initial packing, seed variation, valid source barycentrics and triangle identity, exact default
boundary replay, wet/dry state replay and rejected invalid inputs. Dry material remains geometrically unchanged;
zero dissolution prevents grain detachment. The standalone default removes 1,010 grains by cycle 120, reducing
solid volume from approximately 0.00666303 to 0.00473186 cubic metres.

The existing independent BVH/SAT verifier checked **six snapshots**: fresh and weathered default, fine/dense and
small-source cases. It detected **zero self intersections**. There is one connected patch boundary per snapshot;
this is not a test of interacting free grains or multiple weathering bodies. Reports retain mesh digests and exact
specifications/source snapshots. Shared-boundary contraction and numerical tolerances remain documented in the
intersection verifier.

Two final-source Chromium 133 / SwiftShader browser workflows passed:

- Material: source capture, 120 weathering cycles, picking, all diagnostic channels, palette edits without geometry
  changes, actual JSON download/open with exact geometry replay, pending weather/palette persistence, run/pause,
  packing-change cancellation, dry cycle, invalid-palette rejection, injected worker failure and rebuild recovery.
- Cliff: the existing full workflow was rerun under the new theme, covering all five stages, worker checkpoint
  reuse, late edits, cancellation, OBJ export, exploded-view export stability, legacy recipe import, narrow-angle
  warnings, worker failure and WebGL context loss. Stage-local rebuilding remains intact.
- Both views were checked at 390 × 844 with no horizontal overflow. Browser error lists were empty and both WebGL
  contexts returned error zero before the deliberate context-loss test. No material texture maps were assigned.

The actual captured cliff-face study removes **995 of 2,880 grains** at cycle 120. Water and tracer residuals are
approximately `2.84e-17` cubic metres and `-6.59e-20` tracer units. Differences from the standalone study follow
its captured source orientation rather than a replay mismatch.

Final evidence is in **`VisualProof/CliffSequence/GrainCaptures/`**: eight material/UI captures, the saved study,
`Grains.json`, `Intersections.json` and `Workflow.json`; `CliffWorkflow/` contains fourteen cliff regression captures
and its workflow report. Both browser reports and the grain report have source hashes matching the delivered
application files. The new browser entry point is `VerifyGrainWorkflow.cjs`. Full mesh dumps, dependencies and
intermediate/failed runs remain ignored scratch. C033's earlier evidence is preserved.

## C035 — Fix zoom snapping at fractional browser pixel ratios

**Date:** 2026-10-05. **Base:** C034 (`b4ada1b`). The camera fix is in the shared browser `OrbitControls.js`
under `Experimental/Ocean/lib/addons`, which both cliff views already import. No geometry, material simulation,
UI layout or native code changed. Other browser consumers of this shared helper also receive the correction.

The snapping was reproduced in Chromium at device pixel ratio 0.8. The old zoom calculation divided a CSS-pixel
input by `100 * (devicePixelRatio | 0)`. Ratios below 1 therefore became a zero divisor: a negative wheel event
snapped the camera from 75.082 m directly to its 0.5 m minimum, and the following positive event to its 450 m maximum.

- Removed framebuffer pixel ratio from the wheel/middle-drag calculation. Their input distances use CSS pixels.
- Preserved fractional trackpad deltas and normalized line/page wheel modes into CSS-pixel equivalents.
- Limited each event to 200 equivalent pixels and ignored non-finite distances. At the cliff's zoom speed of 1,
  a single event changes distance by at most a factor of 0.9025 inward or 1.1081 outward, rather than jumping to
  the limits. Existing orbit, pan, pinch behaviour and near/far distance limits are otherwise unchanged.

`VisualProof/CliffSequence/VerifyCameraZoom.cjs` passed in Chromium 133 / SwiftShader for **both workspaces** at
pixel ratios **0.5, 0.8, 1, 1.25, 1.5 and 2**. It exercises 120 synthetic pixel/line/page/fractional/oversized wheel
cases, 12 actual browser-wheel inputs, 12 middle-button drags and repeated movement to both distance limits.
CDP can rescale injected wheel deltas under pixel-ratio emulation; trusted-input assertions use the actual DOM
wheel delta received, while identical synthetic CSS-pixel inputs prove invariant behaviour across ratios.
Browser errors were empty; both WebGL error codes were zero. At ratio 0.8, a 120-pixel wheel event now moves the
cliff from 75.082 m to 70.600 m inward or 79.849 m outward, each measured from the same framed starting position.
The material camera similarly moves from 1.297 m to 1.220 m or 1.379 m instead of its 0.02 m / 6 m limits.

Evidence, before-fix readings and runtime source hashes are in **`VisualProof/CliffSequence/ZoomCaptures/`**.
This fixes the input-induced snap; it does not add camera collision detection or prevent intentionally dollying
inside a formation after repeated input.

## C036 — Native WebGPU triangle radiance cascades, independent demo and review

**Date:** 2026-10-05. **Base:** C035 (`9389529`). New standalone implementation at
**`Experimental/RadianceIntegrator/index.html`**, separate from the existing RadianceSequence, RadianceProjection,
cliff, grain, Fluid and native-engine work. None of those renderers or shared orbit controls changed in this entry.

### Delivered renderer

- Native `webgpu` canvas, WGSL raster and compute pipelines, no WebGL/CPU renderer fallback or editor-style UI.
  The compact responsive HUD provides quality controls, diagnostics, input hints, adapter identification and
  measured pass times. Existing Three.js is used for math/orbit input only, not rendering.
- An original atrium with **2,788 triangles**, **2,134 local BVH nodes**, depth 20 and four rigid instances.
  Two point-light proxies move continuously and the central triangle sculpture rotates. No lightmap baking.
- Screen-origin surface probes trace the complete world-space triangle scene, including off-camera triangles.
  Spatial spacing doubles, angular directions quadruple and finite distance intervals merge coarse-to-fine with
  `Cnear + Tnear * Cfar` and `Tnear * Tfar`. Balanced 640-by-448 storage has **137,344 directional interval slots**.
- One-bounce diffuse source evaluation, explicit emissive triangles/environment, SH9 projection, geometry-aware
  gather, optional mirror-like reflection and reject/clamp temporal reprojection. The Indirect only view is the
  cascade contribution, including directly received triangle emission/environment, not only paths of length two.
- Performance work actually implemented: stackless local BVHs, matrix-only rigid motion, rasterized twelve-face
  point-light depth maps, unchanged-transform shadow reuse, shared per-probe connection visibility, SH9 gather,
  diagnostic pass skipping, optional atomic counters, asynchronous telemetry and a two-frame GPU queue bound.
  The simulation clock advances even when GPU capacity causes a render submission to be skipped.
- Sliders for render scale, probe spacing, angular width, cascade count, first interval, shadow-map size, temporal
  weight, exposure, source power, sky, speed and debug cascade. Presets preserve scene/animation choices. Debug
  views cover direct/cascade lighting, normals, albedo, probe tiles, merged radiance/transmittance, BVH cost and history.

### Research and scope

The requested write-up is **`Docs/RadianceIntegratorReview.html`**, with an equivalent Markdown document.
It reviews three-rc, the Osborne/Sannikov radiative-transfer paper and the accessible Shadertoy description.
three-rc explicitly withholds a license; no code or upscaler was copied. The atrium and WGSL implementation are
original. Shadertoy shader source was not exposed by the page fetch; that portion of the review is description-based.

This remains a **bounded, one-bounce, rigid-triangle research prototype**, not an AAA/open-world renderer. The review
separates implemented optimizations from TLAS/refit, streaming, clipmaps, rebasing, corrected reprojection, multibounce,
rough-specular transport, image-quality evaluation and hardware profiling still needed. Known approximations include
screen-neighbour interval merging, SH truncation, finite angular sampling, biased PCF shadows, view dependence and
low-resolution aliasing. Fixed probe storage does not mean scene-independent BVH traversal cost.

### Executed verification and evidence

`VerifyStructure.mjs` passed **512 CPU brute-force/BVH ray cases** at two rigid poses, triangle/node bounds and leaf
coverage, plus **108 cascade layouts**. `VerifyWebGpu.cjs` ran the actual production pipelines in Chromium 133 through
SwiftShader Vulkan/WebGPU. Final-source execution passed with no recorded page/console errors:

- **512 GPU BVH rays** matched the independent brute-force distances within **4.073e-6 m**. Four CPU-reference rays
  change between poses, so the dynamic geometry is genuinely exercised. GPU interval-composition fixtures pass.
- HDR full/direct/cascade decomposition, all-sources-off exact zero, deterministic exact replay, changed dynamic
  lighting, accepted static history, finite merged buffers, open/blocked transmission and the mirror ray pass.
- **24/24 static emissive triangles** lie outside the test camera frustum. With point proxies and sky disabled,
  the visible-surface HDR cascade mean is **0.001331**; disabling emission makes it exactly zero. The isolated
  image is very dark, so this is quantitative off-screen contribution proof, not a dramatic light-spill claim.
- Quality presets/extremes, real render-scale reallocation, unchanged-shadow reuse, hide/show, pause, wheel zoom,
  continuous animation, bounded in-flight work, portrait layout and explicit unsupported-WebGPU refusal pass.

At 640 by 448, balanced explicit GPU-data allocation is **36,181,072 bytes** (about **34.5 MiB**), excluding driver,
canvas and cache overhead. Five warmed moving-frame software pass sums have median **1,094.15 ms** and range
**1,054.32–1,169.14 ms**. These are **SwiftShader software execution measurements, not gaming-GPU benchmarks**;
real-time hardware performance has not been established. The HUD distinguishes GPU-pass sum from completed-frame
rate and estimated data from measured rays/interval slots. A separately instrumented unchanged frame records
84,566 rays, 4,633,529 node visits and 300,507 triangle tests.

Final browser captures and JSON reports are in **`VisualProof/RadianceIntegrator/Captures/`**, including fourteen
actual rendered/debug/UI images and the source-hashed browser report. Dependencies, full CPU ray fixtures and
intermediate runs remain ignored scratch. The usage README documents static hosting and verifier commands.

## C037 — Actual non-rigid triangle deformation with current-geometry WebGPU GI

**Date:** 2026-10-05. **Base:** `8d63526` (C036 plus retained native CPU evidence). New standalone browser demo at
**`Experimental/DeformationIntegrator/index.html`**. The radiance-cascades atrium and native XPBD tyre remain unchanged.

The request is to demonstrate GI when a quad car/tyre surface actually changes shape, independently of whether
its current vertices come from VAT or a live solver. This demonstration uses a controlled compressing/bulging
quad tyre, not a rigidly transformed sculpture and not a claimed port of the calibrated native XPBD simulation.

- **1,536 tyre vertices / 1,536 tyre quads**; with the open rim and environment, **1,797 quads / 3,594 triangles**.
  GPU vertex deformation changes edge lengths by up to approximately **57.2 mm** in the tested enlarged scene.
- Live positions, a **17-pose rgba32float VAT**, and externally supplied XYZ/XYZW positions enter the same route:
  triangle expansion, geometric-normal recalculation, eleven-depth bottom-up GPU BVH refit, rasterization, tracing.
  The BVH has **2,047 bounds**. Its partition/connectivity stays fixed; changing topology would require rebuilding.
- For a clear geometry/lighting reference, this demo uses **progressive finite-depth diffuse path tracing**, not
  radiance-cascade interpolation. It samples actual area emitters, traces exact triangle shadow visibility and
  follows cosine-weighted diffuse paths. Default depth three includes up to two diffuse interreflections.
- The **frozen-rest comparison** leaves visible geometry current while deliberately giving lighting rays the old
  triangles/bounds. It shows false self-occlusion and changed GI. This is an explicit wrong-result diagnostic.
- Changed geometry/camera/lighting resets accumulation. Static geometry reuses its refit and accumulates samples.
  Quality controls, direct/indirect/normals views, true quad-edge overlay, pause and reproducible pose buttons are
  in a compact non-editor HUD. Unsupported WebGPU is refused; no CPU/WebGL rendering fallback exists.

### Executed verification

`VisualProof/DeformationIntegrator/VerifyGeometry.mjs` passes a nine-pose finite/positive-area sweep, shared quad-edge
incidence, exact BVH leaf coverage and non-rigid edge-length change. The minimum tested area is **0.000270846 m²**.
The deformation is a monotone vertical warp with positive lateral scaling, rather than a singular ground clamp.
This is not a complete intersection study or validation of arbitrary external solver inputs.

`VerifyWebGpu.cjs` passes in Chromium 133 / SwiftShader WebGPU with no recorded browser errors. It reads back actual
GPU vertices, triangle normals and every refitted bound, and checks **960 GPU rays across five live/VAT/external
configurations** against independent brute-force CPU intersections. Maximum distance discrepancy is **1.319e-5 m**;
maximum vertex component error is **2.59e-7 m**. At full compression, **134/192 frozen-rest ray distances** disagree
with current geometry by more than 1 mm. The intermediate VAT pose differs from the analytic deformation by at most
**0.126 mm** per component at the tested 0.373 m load; this is not a general animation-interpolation bound.

An independent CPU area-light integral, using the actual scene rectangles and brute-force shadow visibility,
compares **20 receivers × 64 samples**, including **601 occluded samples**, with the production GPU light estimator.
Maximum RGB discrepancy is **5.58e-8**. Full/direct/indirect decomposition differs by at most **1.42e-7**. Emitters-off
surface RGB and depth-one indirect RGB are exactly zero. Frozen versus current indirect lighting has mean absolute
RGB difference **0.002618**, with identical visible geometry and matched sample indices. Accumulation invalidation,
VAT, external-position refusal, live animation, render-scale changes, portrait layout and no-WebGPU refusal pass.

### Scope, timings and evidence

This is a bounded diffuse reference, not production denoised GI, a new tyre physics implementation, topology-change
support or native SDF integration. Finite samples produce visible noise; finite path depth, ray offsets, float
precision and raster/display sampling remain approximations. The review explains how current-triangle queries
could be combined with static-world SDF queries without claiming that integration has already been delivered.

Three warmed moving-pose software GPU-pass sums at **320 × 216 / one sample / depth three** are **1,302.63, 1,314.90
and 1,297.79 ms**. Vertex/triangle preparation plus refit takes **0.83–16.96 ms** across those samples; path tracing
dominates. These are **software execution measurements**, not gaming-GPU benchmarks or evidence of hardware
real-time performance. The simple renderer has one render in flight and synchronous completion-gated telemetry.

**`Docs/DeformationTransportReview.html` / `.md`** provide controls, mechanism, limitations, integration guidance and
verification details. **`VisualProof/DeformationIntegrator/Captures/`** holds eight actual browser captures, the CPU
and source-hashed WebGPU reports, plus a labelled GIF assembled from captured poses. The GIF is explicitly **not a
real-time speed recording**. Dependencies, full ray fixtures and intermediate runs remain ignored scratch.

## C038 — Actual dynamic signed-field GI, complex bodywork and unobscured diagnostics

**Date:** 2026-10-05. **Base:** C037 (`5cb3dfe`). New standalone experiment at
**`Experimental/DistanceIntegrator/index.html`**. C037's tyre used triangle tracing, not an SDF; this explicitly
corrects that mismatch rather than describing a nonexistent hidden field. Existing demos and native renderers stay
unchanged. The technical explanation is **`Docs/DistanceTransportReview.html` / `.md`**.

### Implemented

- A vehicle-bodywork stress assembly with **11,572 triangles, 5,786 quads, 5,852 vertices and 31 closed components**:
  curved arches, crowned/open hood, cabin openings/pillars, side panels, engine casing, cooling fins, braces and a
  thin undertray. The 25/35 mm features deliberately challenge the volume. This is not the production car asset
  or a calibrated crash/XPBD model; the thicker panels are enlarged 130–160 mm study geometry.
- Actual GPU vertex deformation, normal/triangle expansion and BVH refit, followed by **mesh-derived signed 3D
  volume construction**. Near-first nearest-triangle traversal, component membership and component-AABB sign
  pruning reduce construction work. Per-component crossing parity and CSG-min signed distances handle overlaps
  without treating every component as one odd/even count. The fixed topology has 8,191 bounds, depth 12.
- Actual SDF sphere tracing for shadow visibility and **one-bounce diffuse gathering**. No progressive accumulation,
  recursive multibounce renderer, surface-lighting cache or ReSTIR reuse was added to this browser experiment.
  Static room geometry uses matching analytic box SDFs. A triangle reference remains an explicit comparison mode.
- Default **SDF-only** rendering issues no triangle raster pass. Additional views: field gradient normals, signed
  X/Y/Z slices with physical aspect, mesh-versus-field primary-hit error, march cost/exhaustion, SDF indirect-only,
  raster plus SDF GI, triangle reference, source normals/quad edges and SDF direct-only lighting.
- Deformation/resolution/slice controls, current-field freeze and forced rebuild, render-query counters, revision
  readouts, and a **persistent last-field-build time**. Reusing a paused field does not hide its construction cost.
  External finite position input is supported within the field domain; static-room edits are refused.

### Executed verification

`VerifyStructure.mjs` checks closed component edge incidence, exact leaf coverage, domain containment, finite
normals and positive triangle areas across five poses. Minimum tested area is **0.00115688 m²**; maximum displacement
is **1.06727 m**. This is not a complete arbitrary-mesh intersection/manifold/import guarantee.

`VerifyWebGpu.cjs` executes the GPU and reads all voxel values in four volumes (32³/64³/96³ at amount 0.55 and 64³
at amount 1). All samples are finite and written, with both signs present. **96 selected voxel samples** agree with
independent CPU Voronoi-region closest-point/parity calculations within **0.487 mm**, including half-float storage.
This is not the between-voxel surface error. GPU deformation differs from its CPU double formula by up to **0.047 mm**,
including trigonometric approximation; distance/intersection oracles therefore use the actual GPU vertices.

**720 GPU paired field/triangle rays** are checked. GPU triangle distances match brute-force CPU intersections
within **3.25e-6 m**. In the fixed set of 168 triangle-reference body hits, the field misses **48 at 32³, 8 at 64³,
and 6 at 96³**. Matched-body median depth errors are **18.48 / 5.52 / 2.88 mm**; p95 values are **1,929.48 / 497.25 /
30.58 mm**. The review also includes means and extra hits rather than presenting the improving median as universal
accuracy. Thin features and farther-surface errors remain.

The measured SDF-GI image uses **741,180 marches and zero triangle ray queries**, excluding construction work;
the reference uses **742,381 triangle queries and zero marches**. **539 field marches exhaust the limit** in that
full image: shadows fail dark, bounce failures contribute zero, and primary diagnostic failures are magenta.
Full/direct/indirect decomposition differs by at most **0.001893 RGB** including output rounding; indirect mean RGB
is **0.010240**. All-lights-off surface RGB is zero. Frozen fields remain byte-identical; refreshing them changes
lighting. Rebuilding the same settings reproduces the HDR image exactly.

`VerifyControls.cjs` additionally passes external non-rigid position injection and changed-field detection, invalid
coordinate/out-of-domain/static-edit refusal, exact procedural volume replay, scale adjustment and live animation
with matched geometry/field revisions. Mobile layout and unsupported-WebGPU refusal pass. Browser error lists are
empty. Reports contain source hashes for the delivered code and the shared C037 geometry/shader dependencies.

### Cost and interpretation

All execution used **Chromium 133 / SwiftShader software Vulkan-WebGPU**, not a gaming GPU. Measured construction
intervals are **2,138.42 ms at 32³**, **15,649.39 ms at 64³ / amount 0.55**, **16,642.37 ms at 64³ / amount 1**, and
**50,572.36 ms at 96³**. A reused-field 480-by-328/four-direction GI render sums to **2,227.19 ms** versus
**5,669.37 ms** for the triangle reference. These are individual instrumented observations, not equal-quality
hardware benchmarks.
**Full dense-field reconstruction dominates changing geometry**; the cached-field number is not its dynamic cost.

The review recommends retaining native ReSTIR sampling/reuse, which already has DI/GI reservoir paths, and separately
profiling dynamic-field updates and validity. ReSTIR is not a replacement for intersection geometry, nor can it
recover missing thin panels. Dirty bricks/local fields, faster surface-to-distance updates, thin-surface policy,
reuse invalidation and target-hardware measurement remain future work. No native integration or AAA-readiness claim.

**`VisualProof/DistanceIntegrator/Captures/`** retains seventeen actual browser/debug captures and the CPU, WebGPU and
control reports. Full voxel dumps, dependencies and intermediate attempts remain ignored scratch.

## C039 — Native Project Drive XPBD deformation and tyre SDF evidence

Returned to C++, rather than replacing the C038 browser study or native ReSTIR. Added the requested
`Plans/Ongoing/MasterPlan.md`, native rest SDF1 baking/verification, closed deformation-aware field reconstruction,
and an optional ABI3-compatible geometry extension from the actual Drive solver to the shared host.

The host forks wheel topology and rebuilds normals, culling, traversal and native hybrid distance geometry together;
rigid-only traversal and rest-only coarse errors are disabled for this path. Pause, unchanged revision and exact Stop
restore are executed checks. Rim materials remain rigid. The production renderer remains hybrid SDF/triangle
refinement; the local bake is not yet a streamed GPU object-field cache.

`VisualProof/TyreDeformation/index.html` is a gallery of twelve **C++ CPU field-only renders**, not another browser
simulation or a Vulkan/editor capture. It includes the real Drive DLL front-left snapshot, rest, loaded and banked
fixtures, gradient normals and signed slices. The full-vehicle check measures 6.38 mm of non-rigid displacement beyond
pose-only wheels. Loaded fixture projection agrees with actual XPBD nodes within 0.14 micrometres.

The 64³ ray tests retain their failures: 12/15/12 reference hits missed across the three 1,728-ray fixtures, zero
extra hits, and worst paired depth errors of 632/103/103 mm despite approximately 0.34 mm medians. Local CPU field
construction is about 1.0–1.6 seconds; native synchronized scene reconstruction is not a real-time performance claim.
Production Vulkan presentation, optimized streaming and target-hardware profiling remain unverified/future gates.

GCC execution and host syntax checks pass. The dedicated Windows/MSVC Drive job also passes for `614d469`;
ASan/UBSan/leak checks pass for that implementation. Receipts are retained with the gallery. The full-engine Windows
build is a separate pending job, and none of these CPU receipts is a Vulkan tyre presentation capture.


## C040 — Copied reference inspectors for HTML visual approval

2026-10-05. Copied the deployed `c7egoist/Frontier` inspector implementation at
`f65f2f90f033e17671963c6731ea9e7653bc13b6` into the current experimental Project Zero HTML editor.
`InspectorDepot/Provenance.json` records SHA256 hashes for fifteen byte-unchanged source files, including the
original stylesheet, controls, canvases and requested panel builders. The registry is restricted to this scope:

- Folder: complete reference collection inspector, populated from the current scene hierarchy.
- Wind: identity and flow field through **Anemometer**, inclusive; later reference cards excluded.
- Moon: **Atlas** only, including grid/labels/terminator toggles, terminator dragging and taller view.
- Cloud Layer: identity through **Cloud deck**, inclusive.
- Height Fog: identity through **Light transport**, inclusive.
- Lights: complete Sun, spot, point, IES/automotive, area and tube inspectors. The six authored reference lights
  are added under Lighting: Key Spot, Rim Point, Fill Point, ECE Low Beam, Softbox and Studio Tube.

Existing inspector cards remain below the copies, including duplicate concepts. The existing Area Light also
receives the reference area inspector. New reference-only light rows do not invent an additional legacy inspector.
Saved scenes are migrated once; deleting an added light does not silently restore it on the next reload.
`?inspect=wind`, `world`, `moon`, `clouds`, `height-fog` and the new light IDs provide direct review entry points.

`InspectorHost.js` runs the copied DOM/canvas controls in an auto-height, script-only sandboxed document, preserving
its original CSS without overriding the existing React editor. A script-hash CSP restricts the embedded document.
Visibility, folder expansion and names update the real HTML outliner. Reference properties and notes persist under
`Values[id].ReferenceInspector`, including scene export/import, without reinterpreting the existing inspector units.
This is a **browser-local visual review**, not an engine/rendering connection or a native property conversion.
No C++ inspector changes were made; native porting remains gated on the user's visual approval.

`CheckReference.mjs` verifies the source hashes, thirteen inspector cases, exact requested cutoffs, retained cards,
canvas presence, overflow and card dimensions/styles against independently bundled original source at equal width.
It also executes numeric persistence, literal-name editing, notes, visibility/lock, folder manifest/expansion,
Atlas height growth/shrinkage and layers, IES profile selection, continuous tape dragging, deletion persistence and
cyclic-hierarchy refusal. Thirteen actual browser captures and the receipt are in `Screenshots/Reference/`.
The existing `CheckBrowser.mjs` and `CheckWorkspaceCards.mjs` pass, including the 2,500-record collection fixture.
`CheckWind.mjs` passes when run serially; its first concurrent run had a pointer-placement timing assertion failure.

**Font qualification:** the source's General Sans is still requested from Fontshare's official CDN. Font binaries
are not redistributed. That CDN is unreachable in this sandbox; comparisons and captures therefore use the same
fallback font in both documents. Font failures are reported separately from JavaScript/application errors. These
checks do not establish font-available pixel identity; typography is part of the requested visual review.

The requested actual Project Drive vehicle/tyre **SDF GI** presentation remains the next-turn task. The prior CPU
field gallery is not being represented as vehicle GI, and this inspector change does not alter that renderer.


## C041 — Screenshot-only inspector cards and editable wind flow

2026-10-05. Supersedes C040's request to retain duplicate stacks. The screenshot-selected inspectors now show
only the selected reference cards, without their legacy React inspector appended underneath:

- Sun: hero, four readings, two summary tiles.
- Wind: flow hero, readings, gust/lull tiles and Anemometer.
- Height Fog: hero, readings, summary tiles, Visibility and Light transport. Height-profile card removed.
- Clouds: hero, readings, summary tiles, Coverage and Cloud deck, as in the two additional screenshots.
- Point/spot lights: readings, summary tiles and Photometry; unpictured hero/output/placement cards removed.
- IES/area/tube retain their reading rail; their unpictured distribution, emitter and authoring cards are removed.
  Light entities and saved values are not deleted.
- Moon Atlas is skipped. The original Moon inspector remains. The previously requested Folder design remains,
  without its duplicate legacy inventory appended. Unrelated camera/geometry/environment inspectors are retained.

The wind hero now edits the existing `WindField` model, not a disconnected reference speed/direction illustration.
Arrow dragging sets heading and strength; arrow keys provide numeric adjustment. Tornado mode supports centre
placement, strength and clockwise/counterclockwise circulation. Spline mode has four draggable cubic-Bezier handles
and a 0–100% following control: heading at zero, tangent/cross-track guidance at one. Spatial influence remains
bounded by the component radius. Tangential attraction is removed at path ends so flow exits rather than reverses.
Existing authored composite components, IDs and cloud-consumer data remain intact; fresh fields start with one arrow.

`FlowProjection.js` replaces the old heatmap/vector-grid display with teal flow strokes. Both particle advection and
back-traced stroke curvature sample `EvaluateWind`. The shared canvas is also used by the remaining local-cloud
wind preview/editor. Obsolete heatmap/vector/particle display switches are removed. No extra inspector cards are added;
small mode/strength/heading/following controls live in the existing flow hero.

The Anemometer samples the same field at its marked probe (Alt-click to move it), accumulates a real preview-time
history, retains actual field-change samples, and clears the history when the probe moves. It no longer invents a past minute of gusts. Constant wind
therefore gives a flat trace; retained gust components give varying measurements. Flow motion is explicitly labelled
8x preview time. Field changes persist in browser storage and scene export/import through the existing state format.
This remains a bounded 2D HTML authoring preview, not a native atmospheric solver or engine connection.

The updated `CheckReference.mjs` passes current card-whitelist/absence checks, fifteen original-source hashes,
velocity/path/circulation/degenerate-input checks, real browser pointer and keyboard editing, spline following,
reload persistence, pause/resume, constant/zero Anemometer agreement, trace auto-height and 1024–1920 layouts.
Actual captures and the generated HTML hash are in `Screenshots/Selected/`. Earlier C040 receipts remain historical;
the old broad inspector/wind tests target intentionally removed legacy controls and are not claimed as current passes.
Fontshare remains unavailable in the sandbox; the external-font/fallback qualification from C040 still applies.
No C++ changes or Project Drive SDF-GI rendering work in this revision.


## C042 — Roll back C041; restore original inspectors with additive cards

2026-10-05. Reverted the rejected C041 revision (`7efd292`) to its `b424bc3` parent before applying the correction.
The original Folder inspector is restored, including its inventory, filtering, pagination and notes. The imported
c7egoist Folder panel is no longer registered or rendered. Moon Atlas remains skipped, as requested previously.

All pre-existing inspector cards are retained in their original order. The screenshot-selected reference cards are
appended after that list, not substituted for it. The imported-card filter applies only inside the reference frame:
Sun hero/readings/summary, Wind through Anemometer, Clouds through Cloud deck, Height Fog Visibility/Light transport
and the pictured light readings/Photometry. Newly added reference-only light rows have no pre-existing stack to restore.
Unpictured imported cards are not used as a reason to hide any original editor cards.

The C041 flow replacement, spline controls and shared wind-model changes are rolled back as part of the requested
one-commit undo. Original `WindPanel.jsx` and `WindSpecification.js` are restored byte-for-byte from `b424bc3`.
No saved scene is cleared, and no C++ or renderer changes are included. The C041 notes above describe a rejected,
historical revision, not the current UI. Browser checks and captures for this correction are recorded separately.

Verification: the C042 browser check compares the original card names, heading order and control inventory against
`b424bc3` for World/Showcase/Lighting folders, Camera, Moon, Sun, Wind, Clouds, Height Fog and Area Light. It verifies
that imported additions follow the original stack, with no Folder/Moon import, and checks imported-card whitelists,
Cloud deck persistence, Folder notes and the restored WindEditor. All checks pass. The existing `CheckBrowser.mjs`,
`CheckWorkspaceCards.mjs` (including 2,500-record folders) and `CheckWind.mjs` also pass serially. Receipts and actual
captures are in `Screenshots/Additive/`. Fontshare connection failures remain separately reported, not hidden.
Appended frames are measured synchronously at creation so offscreen rAF throttling cannot collapse their scroll area.


## C043 — Wind, Height Fog and Sun card arrangement

2026-10-05. Refines C042 without removing any original inspector cards or restoring the rejected C041 rewrite.

- Wind begins with the existing combined-field card, immediately followed by Anemometer as the second card.
  Its four statistic cells now have transparent backgrounds and thin separators instead of black tiles.
  Reading pills use two columns in the narrow inline presentation so numbers and units remain legible.
  Existing Wind controls and atmospheric modifiers remain below the imported additions.
- The copied Wind identity with eye/lock controls and the separate wind hero are removed. Reference-style fading
  strokes now sample the existing combined velocity field in `WindPanel.jsx`, retaining its speed colours and legend.
  The existing gradient, vector and particle switches remain in WindEditor; vectors default off for the line preview.
  Directional, gust, radial and tornado calculations, component editing and cloud consumers are unchanged.
- The copied Sun and Height Fog eye/lock identity rows are removed. Their previews and selected additions appear
  above the original inspectors. Height Fog Light transport contains only the beam chamber, without its extra
  title, tape, colour control, toggle and note. All original fog and sun controls remain below.
- Folder remains the original inspector; Moon Atlas remains excluded. Other additive inspector selections are unchanged.

Anemometer retains the copied reference's illustrative speed trace, not a new measurement of the combined field.
Its visibility observer now follows the retained Anemometer, so removing the separate hero does not freeze the chart.
That one-line source adaptation is recorded in `InspectorDepot/Provenance.json`; fourteen other source hashes remain
unchanged. No saved scene is cleared, and there are no C++ or Project Drive rendering changes.

Verification: the standalone build succeeds (4.40 MiB). `CheckReference.mjs` passes 28 checks with no script errors,
including original card/control comparisons against `b424bc3`, the intentional Wind reorder, second-card placement,
transparent statistic cells, live chart animation, preview-first placement, beam-only contents, narrow presentation,
Folder retention and Cloud deck persistence. `CheckBrowser.mjs`, `CheckWorkspaceCards.mjs` and `CheckWind.mjs` also
pass serially. Actual captures, HTML SHA256 and receipts are in `Screenshots/Arrangement/`. Official Fontshare requests
still fail in the sandbox; these captures verify fallback typography, not successful external font delivery.


## C044 — Project Drive GI execution and unified fracture HTML review

2026-10-05. Inspector refinements are paused at C043 at the user's request. Vehicle GI is the first task;
fracture integration follows with HTML approval before a native editor port.

Project Drive: `InterchangeHost.cpp` now exports same-revision execution snapshots of the complete opening scene,
both authored and after 120 actual DLL simulation intervals at 240 Hz. The existing Vulkan scene executor consumes
those snapshots, using CPU BVH primary visibility and the unchanged production SDF lighting shaders. This is not
the earlier isolated-tyre CPU shading gallery. It retains GI-on/off captures and checks for a nonzero image change.
The two captures are independent, not a continuous GPU deformation execution. Diffuse materials preserve authored
base colours but deliberately omit production car-paint specular and transparent glazing. Software Vulkan is not
hardware timing or an interactive Frontier-window capture. Native ReSTIR and the existing raster GI are untouched.
The first execution correctly refused an unsupported one-texel radiance card; the retry uses supported 2×2 cards.
Final execution status belongs to the per-pose receipts under `VisualProof/SdfScene/Drive/`, not this UI check.

`Frontier/Experimental/FractureEditor/index.html` is a standalone, unified HTML authoring review, launched from the
Project-Zero + tab menu. It uses the existing dark, rounded-card/trapezoid-tab design language. A shared asset list,
viewport, inspector and pattern library serve Runtime and Baked workflows; these are not two unrelated embedded demos.
Both supplied branches have overlapping runtime and baked capabilities:

- `arena/01a0f3a8-slate`, pinned at `bc90f99`: live crack-tip propagation, plate-region extraction and the corrected
  elasto-plastic sheet solve with in-plane draw-in, hinge softening, full XYZ positions and deformed normals.
- `arena/01a0f3a4-slate`, pinned at `0cf178a`: energy-limited solid fragmentation, exact half-space clipping and
  material-specific anisotropy. Noise/displacement-based fracture-surface dressing is disabled in this review.
- Seven presets: annealed and tempered glass, concrete, timber, granite, ABS and sheet metal. Steel uses baked
  plastic deformation, not brittle shattering. Its three prescribed tools each provide twelve real damage samples.
- Runtime computes at the chosen impact. Baked mode creates seed variants at a fixed site/energy, then reuses stored
  geometry. Recipe changes invalidate cached patterns. Export contains real geometry and, for metal, damage samples.
- Orbit/zoom, impact placement, separation, wireframe, reset, measured solve duration, volume-error reporting and
  searchable presets work. No arbitrary speed-up or guaranteed gameplay-budget claim is made.

Thirteen imported source modules are unchanged and hash-pinned in `SourceDepot/Provenance.json`. This combines
selected numerical components for the HTML review; it is not yet the complete native fracture system. Jolt debris
and breakable constraints, native GPU/VAT playback, editor-created arbitrary geometry, support-island release and
native asset serialization remain integration gates. Shell extraction retains all qualifying regions, rather than discarding them at the solid-fragment ceiling.
Coverage explicitly reports missing crack cells and small specks; this is not mass-conserving collision geometry.
Material values and prescribed
metal tool travel are research/demo parameters, not validated engineering failure predictions.

Verification: fourteen browser checks pass with no page errors, including both algorithms, cached pattern reuse,
recipe invalidation, geometry export, plastic XYZ motion and damage scrubbing, reset and 1024/390-pixel layouts.
The Project-Zero launcher and its existing browser regression also pass. Actual captures and receipts are under
`FractureEditor/Captures/`. No C++ fracture editor has been ported before visual approval.


C044 executed vehicle result: Actions run `37375125331`, shader/source revision `4eee806`, completed the loaded
Project Drive GI-on/off case on llvmpipe. All 22,046 facets and 22 instances were present; the real DLL again proved
6.37654 mm of non-rigid tyre departure beyond pose-only motion. Both 512×320 images are retained, with no Vulkan
validation errors. Independent PNG readback checks agree with the native comparison: RGB RMS 3.5992988/255,
58,325 changed pixels and 716 distinct GI-on colours. The loaded job passed; this is not a hardware-speed result.

The authored/rest case produced its GI-on image but exceeded the 2,700-second execution limit during the remaining
GI-off work. That job and therefore the overall workflow failed; a complete authored comparison is NOT claimed.
The viewer defaults to the passing loaded case, labels the authored GI-on image as incomplete, and does not present
a nonexistent authored GI-off image. `VisualProof/SdfScene/CheckDrive.mjs` verifies the loaded comparison and these
partial-output guards. The remaining native-window, continuous-deformation and full-material gates are still open.


## C045 — Object-specific fracture and closed geometry

2026-10-06. Supersedes C044's fracture gallery after the user's topology review. The vehicle GI receipts and
accepted C043 environment inspectors are unchanged. Sheet metal, deformation tools and their active workflow
are removed; the upstream source snapshots remain untouched for provenance.

- Select Sphere, Cube, Cylinder or Cone in Project-Zero. Its inspector now has **Enable fracture**, a
  **Dynamic / Baked** section and an expand button. The outliner context menu also enables/disables fracture.
  Expansion opens that scene ID's fracture editor, not a global material-example gallery.
- Source primitive and scale are separate from fracture material. Wood, Stone, Concrete, Glass, Tempered glass
  and ABS change the response without replacing the selected shape. Position/rotation are excluded because
  authoring operates in local space. The existing HTML viewport contains analytical markers, not native mesh
  buffers; this revision constructs the matching tessellated primitive and applies its authored scale.
- The expanded editor uses the actual main-editor slider CSS, its embedded DM Sans fallback fonts and rounded
  dark cards. Material resistance/density, impact, fragment quality, bake and geometry receipts have dedicated
  cards. The impact/quality diagrams are explanatory, not measured stress fields. Font notices are embedded.
- Runtime generation and stored replay use the same closed geometry. Each scene ID owns its recipe and an
  IndexedDB triangle-geometry receipt. Geometry/material/recipe changes invalidate replay; presentation changes
  do not. Reopening, cross-window changes, duplication, disabling, removal and baseline restoration respect
  ownership. Duplicates never inherit another ID's baked receipt. Export includes a matching triangle pattern
  and explicitly declares that it is not a native asset.

The missing glass came from raster crack-region extraction, which omitted crack cells and small regions.
That active path is replaced with paired half-space cuts covering the entire source. Crossings are shared per
edge; cuts through existing vertices/edges retain their cap boundaries. Coplanar primitive patches are joined
before subdivision. Every accepted split must conserve signed volume and produce closed, outward triangles.

Triangulation compares a maximum-minimum-quality diagonal solution with an area-centred interior fan and keeps
whichever has the better weakest triangle. Boundary vertices, including collinear seams, are retained. Added
interior points are coplanar; no surface displacement or shading trick supplies missing geometry. Exported and
stored faces are triangles. Cut acceptance also checks split balance, minimum source-axis spans, triangle quality
and source-normalised fragment slenderness. Naturally thin source dimensions are exempt from an impossible
absolute thickness requirement; wood retains a larger slenderness allowance for grain-directed fragments.
Refused cuts leave the original piece intact rather than deleting it or forcing the requested fragment count.

Verification:

- `CheckGeometry.mjs`: **168** shape/material/seed cases pass, plus cuts through vertices/edges, scaling,
  determinism, zero-energy arrest and explicit concave refusal. Independent checks cover triangle-edge incidence,
  opposite winding, Euler characteristic, convex containment, float32 vertex collapse and sampled coverage/overlap.
  Maximum relative volume error is **5.72e-16** before upload and **2.87e-8** after float32 upload. The material
  matrix checks 75,600 volume samples. The lowest triangle-quality score is 0.0141, including thin plate sides.
- `CheckBrowser.mjs`: **21** checks pass with no application errors, including the selected-sphere flow, source
  retention under material changes, actual bake download, reopen/replay, per-object isolation, source-scale
  invalidation, outliner actions and 1024/390-pixel layouts. Glass reassembly is captured at 100.000% occupied volume.
- Project-Zero's existing broad browser regression passes. External Fontshare failures remain reported separately
  there; the fracture editor embeds the existing licensed fallback fonts and needs no font network requests.
- All thirteen upstream hashes still match `SourceDepot/Provenance.json`. Captures and current receipts are under
  `FractureEditor/Captures/`; obsolete gallery/metal captures are removed. C044 evidence remains in Git history.

Limits: this is convex-primitive HTML authoring, not arbitrary native geometry import or a calibrated material
failure simulation. Torus/concave inputs are refused instead of filling holes with a convex hull. ABS plastic
strain, reinforcement, Jolt debris/constraints, native geometry ownership and native bake serialization remain
future integration work. Native fracture implementation still waits for visual approval.


## C046 — Quieter fracture cards and shaded illustrations

2026-10-06. Presentation-only follow-up to C045, requested after visual review.

- Removed the analytical-preview/native-execution paragraph from the selected object's Fracture card.
- Removed the expanded editor's Geometry Preservation heading, slogan, response explanation, three guarantee
  bullets and long analytical-preview disclaimer. Object properties and functional status remain.
- Replaced the outlined cube-to-shards diagram with a shaded, separated fracture illustration. The black inset
  tile, arrow and caption are gone.
- Replaced Fragment quality's wire polygon and dimension bracket with a shaded fragment-sizing illustration.
  Fragment ceiling and Minimum span change the illustrative subdivision and spacing. These are compact vector
  illustrations, not new geometry solvers or measured simulation results.

The selected-object workflow, material responses, sliders, dynamic generation, stored geometry and unsupported-input
refusals are unchanged. No native integration or unrelated environment-inspector changes are included.

Verification: both standalone builds succeed. The fracture browser check passes 22 checks, including explicit
absence of the removed blocks, both new graphics and their response to the two sizing controls, plus the existing
per-object bake/replay and ownership checks. Actual 1366 × 720 captures are `Captures/FractureCard.png` and
`Captures/QualityCard.png`; the standard desktop and narrow captures are refreshed by the same browser check.

## C047 — Square statistics, complete light cards and per-piece SDF authoring

2026-10-06. The user explicitly selected **HTML-first** for this pass. No native fracture, SDF generation or C++
inspector port is included.

### Statistics and Light-category cards

- Sun, Wind, Height Fog and the five light types use square-cornered statistic tiles. Their metric rails use two
  columns, wrapping rather than ellipsizing values. Card shells and slider tracks retain the existing dark theme.
  These changes are scoped to the requested inspector types; Cloud/Moon/Folder styling is not changed.
- Owned `LightPanel.js`/`.css` instruments replace the incomplete imported light stubs at the host adapter boundary.
  The pinned `InspectorDepot` files remain unchanged. The original native Area Light controls remain alongside the
  new cards, including their original intensity units; native lux and authored luminous flux are not equated.
- Point: radial emission, intensity, estimated falloff, reach and decay. Spot: cone/pool illustration, full cone angle,
  penumbra and aim. IES/automotive: illustrative distribution profiles, colour temperature, multiplier, field angle,
  cut-off and range. Area/softbox: aperture, width/height, spread and two-sided emission. Tube: linear emitter,
  length, radius, temperature and reach. All types include tint, placement and participation settings; applicable
  types include a target, and tube includes rotation. Placement has an X/Z schematic.
- Number-entry pills and sliders use scoped copies of the main editor's control styling. Properties use the existing
  inspector bus and browser persistence, including locking. Blank/unfinished number entries restore the saved value
  on blur; negative positions and cut-off can be entered. No new engine-light binding is claimed.
- Diagrams are explicitly schematic, not photometric validation. Automotive presets do **not** load measured IES
  data, certify ECE/SAE compliance or simulate shadows; custom IES import remains pending. Participation flags are
  authored settings, not proof of native lighting execution.

### Geometry-object fracture and requested SDFs

All geometry records retain the Enable → Dynamic/Baked → expand workflow, including concave and imported scene
entries. The main card now distinguishes unavailable imported-source previews from concave decomposition needs.
The expanded editor continues to refuse unsupported execution rather than generating a replacement box. This is
not an arbitrary-mesh importer or a newly implemented concave fracture solver.

Baked mode adds **Bake SDF per piece** with **32³ / 64³ / 128³**, planned R16F, per-fragment resolution. The request
and resolution persist per object, synchronize between the main and expanded editors, and travel in recipe exports.
They are deliberately excluded from the browser geometry signature, so changing an SDF request does not invalidate
otherwise-current triangle fragments. Existing signatures and stored geometry remain compatible.

When requested, the button says **Bake geometry** and a successful triangle bake reads **Geometry ready · SDF
pending**. Export explicitly records `Sdf.Requested`, `PerFragment`, `Resolution`, `Format` and **`Generated:false`**;
there are no distance samples or native-compatible artifacts. The setting is not a working browser SDF bake.

### Executed checks and captures

- Both standalone HTML builds succeed.
- `CheckLighting.mjs`: **22 checks**, no application errors. Covers seven light entries, live diagrams, square
  readings, reload persistence, tint, IES presets, negative cut-off, aim, sidedness, shadow flags, locking, incomplete
  number input, a 240px light inspector, Sun/Wind/Fog cards, all five scene primitive entries plus an imported geometry
  record, SDF normalization, main/expanded synchronization, real triangle bake and truthful ungenerated-SDF export.
- Existing `CheckReference.mjs` regression passes with `Errors: []`, retaining the native control inventories,
  accepted Wind order/composite, Sun/Fog arrangement, Folder and Cloud behavior. Pinned inspector hashes match.
- Fracture `CheckGeometry.mjs`: **168** cases plus plane/scaling/determinism/refusal checks still pass.
  `CheckBrowser.mjs`: **22** existing per-object bake/replay/ownership/layout checks still pass. The fracture source
  depot remains hash-pinned and unchanged.
- Actual browser captures and the new receipt are in `ProjectZeroEditor/Screenshots/Lighting/`, including each light
  type, environment tiles, the main SDF request and expanded geometry-ready/SDF-pending status. External Fontshare
  requests fail in the sandbox and are separately recorded; these captures use fallback fonts.

Native follow-up remains gated on approval: real geometry ownership/import/decomposition, runtime fracture and
contacts/supports, individual fragment SDF generation with independent bounds/transforms and valid distance/sign
samples, and native serialization/invalidation. GPU flood-fill/component labeling can identify disconnected voxel
components when that representation is used; it is not a universal prerequisite for fracture or for baking already
separated pieces.

## C048 — Quieter Anemometer, header-first cards and shared light transforms

2026-10-06. HTML-only follow-up to the user's C047 review.

- Kept the Anemometer's live trace, instantaneous speed and mean line. Removed its gust-factor, spread, pressure and
  alternate-unit block through the owned host adapter; the pinned Wind source is untouched.
- Statistic tiles on Sun, Wind, Height Fog and lights now have **no outline** and **6px corner rounding**, replacing
  C047's sharp bordered boxes. They retain the two-column layout and readable values.
- Sun and Height Fog now show their native inspector header **before** the added preview/statistic cards. The
  additions align with the native card width. Wind's approved composite-first / Anemometer-second order stays intact.
- Replaced each light's custom Placement & aim card and X/Z diagram with the existing **TransformPanel** component:
  Position, Rotation and Scale, including numeric entry/drag, resets and locking. The emitter adapter derives an
  initial rotation from legacy target-based lights, preserves aim when translating, and updates the stored target
  when rotation is authored. This remains browser authoring, not a native light-transform integration.
- Added appropriately adapted Atmospheric Fog and Local Fog instruments: sight-line/volume schematic, borderless
  readings, visibility probe and beam chamber. They use each object's existing native-draft controls and shared
  HTML extinction model, not independent copied height-fog values. Atmospheric Fog exposes distance start and Mie
  blend; Local Fog exposes coverage, feature scale, anisotropy and its authored shape. Its transmission probe is
  explicitly a homogeneous interior study, not an integration through arbitrary volume geometry. Existing medium,
  bounds, wind and other native controls remain.

### Duplicate environment entries

In this HTML editor, built-in **Sky** and **Atmosphere** both selected the same atmosphere inspector and parameter
schema. The default scene now contains one **Atmosphere** entry. Saved/imported legacy Sky entries consolidate into
it, children are reparented, and old `?inspect=sky` links redirect. Explicit canonical Atmosphere values take precedence;
other authored Sky values are carried over. The original Sky property record is retained in scene/export values so
conflicting drafts are not silently destroyed. Visibility/collapse state transfers only when Sky was the sole entry;
reset canonical values do not resurrect the archived draft on a later reload. Non-alias/user-created objects remain.

The three fog entries are not duplicate inspectors: Height Fog has altitude-dependent falloff, Atmospheric Fog has
surface-distance/aerial haze controls, and Local Fog has bounded geometry. All three are retained.

### Verification

- Standalone build succeeds. `CheckInspectorLayout.mjs` passes **10 checks**, including border/radius/header order,
  continuing Anemometer animation, both new fog variants and native-control persistence, narrow layout, shared light
  transforms/reset/lock, and legacy Sky migration. An explicit comparison against C047 checks the complete native
  input/select/textarea inventories for seven affected inspectors; none are lost.
- Updated `CheckLighting.mjs` passes **22 checks**, including all light types, persisted transform values and the
  existing fracture/SDF-authoring workflow. Existing `CheckReference.mjs` passes with no application errors and
  unchanged pinned inspector hashes. External Fontshare failures remain separately reported by the browser tests.
- Actual browser captures and receipts are in `ProjectZeroEditor/Screenshots/InspectorRefinement/`, including
  `Anemometer.png`, `sun-header.png`, `height-fog-header.png`, both added fog variants and `LightTransform.png`.

No native C++ port, fracture geometry algorithm or SDF-generation capability changes in this pass.

## C049 — Replace Cloud visuals and bring Cloud/Light additions forward

2026-10-06. HTML-only arrangement follow-up to the supplied Cloud and Area Light screenshots.

- The global Clouds inspector now starts with its normal text header, followed by the ported map/statistics and
  Coverage card. The old Cloud coverage illustration is replaced, not left alongside a second coverage card.
  The existing native Coverage number/range control remains, now joined to the replacement card.
- Cloud base now uses the ported deck's shaded-band visual instead of the old CloudAltitude illustration and
  redundant generic parameter plot. The owned adaptation fits native base and thickness values rather than clipping
  everything above the reference's fixed 400 m scale. The base control remains; dragging the visual and keyboard
  arrows also edit base altitude. Cloud settings follow this top instrument group. Local Cloud is unchanged.
- Cloud summary and coverage are separate views of the same pinned reference panel, not two independent recipes.
  Coverage and Base share values with the retained native controls. The reference's normalized optical-density
  setting maps to the native draft's 0–4 density range; this is a UI-range mapping, not physical calibration.
  Explicit native values take precedence; legacy imported values are used when no native value was authored.
- Cloud statistic tiles now use the reviewed borderless, lightly rounded two-column treatment, avoiding clipped
  base-altitude readouts at narrow widths.
- Area Light and every added light type show their new cards immediately below one plain Lighting header, ahead
  of the old native controls/notes. The duplicate icon/name/type/eye/lock identity strips are removed from Clouds
  and lights. Light Transform and original native Area Light controls remain. Previously locked lights or Clouds
  have a conditional text unlock action, so removing the strip does not strand an existing locked draft.

Verification: standalone build passes. `CheckCloudPlacement.mjs` passes **15 checks** with no application errors:
card replacement/order, both directions of coverage synchronization, reload persistence, base/thickness changes,
base pointer/keyboard editing and full-range display, all seven light entries, legacy cloud values/locks, untouched
Local Cloud and narrow layout. A comparison against C048 retains the Clouds, Area Light and Local Cloud native
input/select/textarea inventories, excluding the deliberately removed read-only identity text.

The existing reference regression passes **28 checks**, lighting/fracture-authoring regression **22 checks**, and
header/transform/environment-migration suite **10 checks**. Pinned InspectorDepot hashes remain unchanged. Browser
captures and receipts are in `ProjectZeroEditor/Screenshots/CloudPlacement/`; external Fontshare failures remain
separate from application errors. No native engine, fracture algorithm or SDF-generation changes are included.

## C050 — Anemometer first, matching fog sightlines and redesigned light instruments

2026-10-06. HTML-only follow-up to the user's Wind/Fog screenshots and two charcoal-dashboard JPG references.

- Wind now runs **normal heading → Anemometer and statistics → Composite Flow / Wind field → Wind controls**.
  This explicitly supersedes C048's composite-first order. The live trace, accepted composite colours, removed
  secondary readouts and original controls are unchanged.
- Atmospheric Fog and Local Fog now use the Height Fog hero's dark 166px sightline, perspective floor, receding
  contrast gates, dashed sight marker and overlaid caption. Removed the separate blue caption/volume-hero treatment.
  The retained visibility probe, beam chamber and native controls still use their existing parameters and extinction
  calculation. Local Fog retains its native shape/bounds controls; the hero is labeled an **interior probe**, not a
  bounded ray integration. Zero extinction does not incorrectly display an enabled medium as disabled.
- Height Fog, Atmospheric Fog and Local Fog remain separate. Height Fog controls altitude-dependent density;
  Atmospheric Fog controls distance/start haze and Mie blend; Local Fog supplies bounded-medium authoring. They can
  describe overlapping atmospheric effects, but they are not the same parameterization. No fog models were merged.

### Light redesign and source families

The two supplied JPG dashboards, rather than the editor screenshots, guide the new charcoal surfaces, thin large
numbers, faded decimals/units, quiet labels, coloured metric ticks and sparse plots. Each family has its own source
study and derived readings, not the former shared room preview with different labels:

- **Point:** analytic attenuation with a logarithmic distance axis, sample illuminance estimates and authored reach.
- **Spot:** cone/penumbra envelope, full-cone and soft-edge readings.
- **IES:** synthetic polar distribution with architectural Downlight, Wall wash and Batwing choices alongside the
  retained automotive presets. Added an **IES Downlight** scene/palette entry. Flux, multiplier, temperature, cone,
  cut-off pitch and range remain editable. The card explicitly says the presets are illustrative and `.ies` file
  import is pending; these are not measured photometric samples.
- **Area:** luminous aperture with rectangle/disk selection, dimensions, spread and one-/two-sided emission.
- **Tube:** linear emitter with length/radius study and linear-output reading.
- **LED emitter:** package/optic study, driver wattage, efficacy target, dimmer, temperature, diameter and emission
  angle. Estimated flux is watts × efficacy × dimmer, not measured output or a thermal/electrical simulation.
- **LED strip:** distinct ribbon diagram with Straight/Cove/Ring routing and diffuser. Controls include length,
  lumens/metre, watts/metre, emitters/metre, voltage, temperature and dimmer. Readings show estimated output,
  connected load and emitter count. Routing is an authoring schematic, not generated scene geometry; the drawing
  caps its displayed sample count while the numeric count retains the full authored length/density calculation.

Sun remains the directional source. Seven owned SVG source glyphs appear in the outliner, Construct palette and
light cards. The ten light entries retain header-first placement without copied name/eye/lock identity strips.
All use the standard Transform panel, including reset/locking. Original native Area Light controls are preserved.
Construct can author the new light sources before placement; editing a palette draft does not change its scene
counterpart. Existing source types, native controls and C049 Cloud changes remain intact.

Saved/exported scenes carry `LightDesignRevision: 1`. C049 scenes receive only the three new defaults once; previously
removed old lights are not resurrected. Deleting a new source after migration also persists. User-created lights,
authored values and source-specific icons survive reload and scene JSON export/import.

### Verification

Standalone build: **4.62 MiB**. **99 browser checks** pass with no application errors:

- `CheckLightDesign.mjs`: **21** — distinct diagrams/icons, all seven families at 240px width, LED/strip estimates and
  routing/diffuser, IES lobes/disclosure, area aperture, LED locking, Construct pre-placement editing, JSON round-trip,
  one-time migration/deletion, Wind order and both native-driven fog sightlines.
- `CheckLighting.mjs`: **25** — all ten light entries, live controls/reload, Transform, locking, tint and unchanged
  fracture/SDF-authoring workflow. Updated light-tile expectations reflect the explicitly requested dashboard redesign;
  Sun/Wind/Height Fog retain their reviewed borderless 6px tiles.
- `CheckReference.mjs`: **28** — retained reference/native arrangements, source hashes, Wind trace and original Folder.
- `CheckInspectorLayout.mjs`: **10** — header order, fog parameters, Transform/reset/lock and environment migration.
- `CheckCloudPlacement.mjs`: **15** — retained C049 Cloud replacement/synchronization and original native controls.

Native control comparisons use C049 (`2112773`). All pinned InspectorDepot files remain unchanged. The checks wait
for asynchronous stored Transform edits before injecting legacy scene fixtures. Actual browser screenshots and
receipts are under `ProjectZeroEditor/Screenshots/LightDesign/`; Fontshare request failures are recorded separately
and the captures exercise fallback fonts. No native engine port, fracture algorithm, SDF generation or native light
rendering is introduced by this change.

## C051 — Shared Cloud cards, library icons and expanded light dashboards

2026-10-06. Follow-up to the user's expanded-inspector screenshots and rejection of C050's stretched light cards.

### Clouds and Local Cloud

- Both inspectors now use the same header-first summary/map/statistics, Coverage histogram with native Coverage
  input, Cloud base/deck and Layer thickness card designs. Removed Local Cloud's old standalone coverage visual.
- Local Cloud keeps its original Centre, Half Size, body, enable and wind controls. Its deck base is derived from
  `Centre Z − Half Size Z`; thickness is `2 × Half Size Z`. Moving its deck translates Centre Z rather than inventing
  independent local Base/Thickness properties. Negative elevations and large bounds are supported. The pointer
  gesture freezes both plotting span and lower datum until release.
- The shared reference adapter now distinguishes global and local parameter defaults. Coverage and density use the
  same mapping in both directions; each scene object retains independent values. The Local Cloud map explicitly says
  **LOCAL VOLUME · SCHEMATIC**, rather than presenting the reference's fixed 12km swath as its authored dimensions.
  These remain browser authoring studies, not native volumetric renders.

### Existing icon library

- Removed C050's hand-drawn light glyph collection. Source rows, Construct entries and light cards now reuse the SVG
  assets already shipped in `Frontier/EngineContent/Icons/`; no new icon files or icon dependency were introduced.
- Restored `editor-area-light` for Area Light and Softbox, including existing saved scenes. Point and spot use their
  shipped editor icons. Other sources reuse the closest existing light assets; these are not newly invented symbols.
- The standalone build embeds 161 library icons again, rather than C050's 168 library-plus-custom icons. Browser checks
  compare each displayed light icon's data URI to the corresponding shipped asset and verify successful image decode.

### Light cards

- Rebuilt the expanded inspector as two compact, independently flowing columns: the source study and optical controls
  on the left; output controls, readings, analytical response and standard Transform on the right. A docked inspector
  returns to one ordered column. The large empty banner and isolated thumbnail shown in the user's screenshots are
  replaced by bounded cards with controls and readings visible alongside the illustration.
- Redrew the source studies: projected area aperture with dimensions and emission envelope; two-section IES polar
  diagrams with labelled guides; LED package with contacts, emitter array and optic arc; LED ribbon with routing,
  terminals and segment marks; spot cone with soft edge/cross-sections; point reach guides; and a dimensioned tube.
  The diagrams are fitted without stretching their coordinate system across the full inspector width.
- Added source-specific analytical cards: aperture flux/area, angular preset response, electrical current budget,
  dimmer/conversion study, beam diameter at 5m, authored distance falloff and linear output. These are labeled estimates
  or analytical studies, not measured/native telemetry. Disk aperture area uses the ellipse area factor. IES preset
  curves remain synthetic; file import remains pending, and a stored Custom IES selection displays no measured samples.
- Existing source controls, saved properties, Construct editing, standard Transform and locking remain. Statistic
  tiles return to restrained borderless 6px corners. No Sun/Wind/Fog or native rendering redesign is included.
- `?workspace=inspector` opens the expanded inspector directly for review; normal links retain the three-column view.

### Verification

Standalone build: **4.63 MiB**. **113 browser checks** pass with no application errors:
`CheckSharedCards` 14, `CheckLightDesign` 21, `CheckLighting` 25, `CheckCloudPlacement` 15,
`CheckReference` 28 and `CheckInspectorLayout` 10. The expanded-entry query also passed a separate browser smoke check.

The new checks cover both Cloud layouts and bidirectional Coverage editing, local bounds-derived base/thickness,
negative altitude and deck keyboard persistence, independent global/local records, exact library-icon reuse, and all
nine reviewed light entries in expanded and 240px layouts. Expanded checks assert side-by-side placement and compact
control/reading gaps, not merely absence of horizontal overflow. Existing suites retain light editing/locking/JSON
round-trips, migration/deletion behavior, C049 global Cloud behavior, Wind/Fog arrangement and fracture/SDF authoring.
Native control inventories are compared against C050 (`4068a2a`); pinned InspectorDepot hashes remain unchanged.

Actual screenshots and receipts are in `ProjectZeroEditor/Screenshots/SharedCloudLighting/`, including the expanded
Softbox/IES/LED views and both Cloud modes. Fontshare network failures remain separate from application errors;
fallback fonts were exercised. No native engine, fracture algorithm, SDF generation or measured photometry is added.

## C052 — Neutral Anemometer, matching beam chambers and softer statistic corners

2026-10-06. Targeted styling follow-up to the supplied Wind/Fog screenshots and dashboard JPG.

- Removed the Anemometer's green rectangular gust band and green trace. The existing live samples now draw a faint
  neutral-grey under-curve wash, subdued grey line, white above-mean sections and white peak/latest markers, with one
  restrained amber trough marker. Mean/axis guides, sixty-second history, sampling, animation and taller-trace control
  are retained. Wind's composite colours, card order and numeric readings are unchanged.
- Removed the Height Fog beam chamber's black canvas fill, black wrapper and inset border. Its transparent drawing
  now reveals the same charcoal card background as the surrounding imported cards. Atmospheric and Local Fog beam
  drawings likewise reveal their containing instrument surface. Beam/scatter calculations and labels are unchanged.
- Increased compact environment statistic corners from 6px to **12px**, keeping them rectangular and borderless.
  This applies to both statistic rows on Wind, Sun, Height Fog, Clouds and Local Cloud, plus the corresponding compact
  Atmospheric/Local Fog readings. Large cards, light-card styling, native controls and other layouts are untouched.

The owned `InstrumentSpecification.js` applies checked presentation substitutions during bundling. Each source anchor
must match exactly once or the build fails. Pinned InspectorDepot files remain byte-identical; only the generated
Wind-trace palette/drawing decoration and Fog-chamber background differ from those sources.

Verification: standalone build succeeds (**4.63 MiB**). **74 checks** pass with no application errors: 11 focused
instrument-style checks, 10 inspector-layout/native-control checks, 28 reference checks and 25 lighting/authoring
regressions. Pixel checks verify no green Anemometer pixels, neutral highlights and the amber sample, continued
animation, taller/narrow views and transparent chamber corners. Computed-style checks verify 12px environment corners,
retained borderless cards and unchanged 6px light tiles. Native control comparisons use C051 (`0d69d4b`).

Actual browser screenshots and receipts are in `ProjectZeroEditor/Screenshots/InstrumentStyle/`, including
`windWide.png`, `WindTraceTall.png` and `HeightFogChamber.png`. External Fontshare failures are recorded separately;
fallback fonts were exercised. No native rendering, fog-model, wind-simulation or saved-property changes are included.

## C053 — Light drawing, source response and inspector order

2026-10-06. HTML-only follow-up: replace the Area Light drawing, combine source and response, and audit every inspector.

- Replaced the Area Light perspective plane/frustum with an orthographic front-elevation aperture drawing: authored
  width/height dimensions, centre guides and a restrained dotted surface. Rectangle/Disk, dimensions, tint, flux and sidedness
  continue to update the illustration. The surrounding Luminous surface card, shipped icon and authoring controls remain.
- Moved each light's analytical response inside its source/output card. Point lights now have a labeled illuminance
  axis, distance guides, exact 1/2/5/10 m sample markers, a highlighted 5 m sample and an accessible numeric table.
  Table values and chart scale follow authored intensity and decay; percentages are relative to the 1 m sample.
  This remains an authored free-space estimate, not measured photometry or a native scene-lighting calculation.
- Light layout now reads heading, main card, statistics, participation switches, then source/optics/Transform controls.
  Expanded views keep the summary stages full-width and the lower source/control columns independent; docked views
  use one column. The response is no longer a separate card. Native Area Light uses summary/detail reference slices
  so its original quick-control grid also precedes the detailed imported controls. Native fields and notes remain.
- Sun, Atmosphere, Lens Flare, Stars and Rainbow baking cards now finish their inspectors. Stars' unavailable Bake /
  Use baked buttons are no longer mixed into its quick-control row. Atmosphere's Dome Path and Sheet controls move
  with Fetch Baked Dome into the final Baked atmosphere card. Geometry's fracture execution/per-piece SDF authoring
  is the final section, after material controls and notes. Availability and pending-native messages are unchanged.
- Reordered Cloud/Local Cloud settings before coverage/deck/body controls; Wind quick controls before Wind field;
  Moon phase before settings/catalogue; Stars' field before quick controls; precipitation type before emission flags;
  and Rainbow's optical preview before visibility controls. Flare switches are together below the composite.
- Height Fog uses summary/detail slices to place quick controls after both statistic rows, before visibility and beam
  diagrams. Atmospheric/Local Fog insert their existing quick controls at the same boundary. Distinct fog models remain.
- Sun, Geometry and Post Process use the normal breadcrumb/title/subtitle heading; Folder gains the breadcrumb while
  retaining its original collection inspector. Sun's Enabled action moves into its quick row. Geometry's existing
  Transform is its primary data card. Control-only Post Process starts with quick controls; absent statistics/previews
  are not fabricated. Camera, Folder and the remaining existing sections were checked rather than redesigned.

### Inspector audit

The new order suite checks **all 35 default scene entries**, in **both docked and expanded layouts**, across these
18 panel families: Folder, Geometry, Camera, Post Process, Atmosphere, Sun, Lens Flare, Moon, Stars, Wind, Height Fog,
Atmospheric Fog, Local Fog, Clouds, Local Cloud, Precipitation, Rainbow and Light. Light entries exercise all seven
source families. Assertions cover rendered vertical order, main-frame and iframe overflow, merged-response ownership,
independent expanded control columns, terminal baking sections and the absence of controls after the final bake card.

Verification: standalone build succeeds (**4.64 MiB**, 161 shipped icons, 57 native glyphs). **212 checks pass** with
no application errors: 89 inspector-order/data checks, 21 light-design checks, 25 lighting/authoring checks,
11 instrument-style checks, 10 inspector-layout checks, 28 reference checks, 14 shared-card checks and 14 Cloud/Light
placement checks. Native editable input inventories for all 18 panel families match C052 (`dd92a6e`) after ignoring
order. Existing suites verify Construct, scene export/import, locking, persistence, local Cloud bounds and negative
altitudes, C052 palettes/corners and unchanged pinned InspectorDepot files. Layout expectations were updated explicitly
for the authorized merged response, summary/detail slices and revised section order.

Actual browser screenshots and receipts are in `ProjectZeroEditor/Screenshots/InspectorOrder/`, including
`AreaDocked.png`, `AreaExpanded.png`, `PointResponseExpanded.png`, `SunBakingLast.png`, `AtmosphereBakingLast.png` and
`StarsBakingLast.png`. The response screenshot demonstrates an edited 50 cd / decay 1 source; default Fill Point remains
10 cd / decay 2. External Fontshare failures are recorded separately; fallback fonts were exercised. No native renderer,
fracture algorithm, SDF computation, source-property schema or Construct behavior was changed.

## C054 — LED strip direction and atmospheric-fog visual (2026-10-06)

- Simplified the LED strip projection to one horizontal run. The earlier synthetic Cove and Ring routing choices were
  removed; length, emitter density, supply, diffuser, photometric estimates, locking, persistence and Transform remain.
- Replaced Atmospheric Fog's nested distance boxes with a layered landscape visibility study. Receding ridgelines,
  trees, a road/centre line, distance ticks and extinction-driven contrast now communicate aerial perspective directly.
  Local Fog retains its bounded-volume sight-line treatment.
- This remains an HTML inspector visualisation. It does not claim a native renderer capture or measured atmospheric
  data.
- Validation: 149 focused checks passed (89 inspector-order, 28 reference-preservation, 21 light-design and 11
  instrument-style), with no reported errors. The generated standalone remains 4.64 MiB with 161 shipped icons and 57
  native vector glyphs.

## C055 — optional entity notes and contextual outliner values (2026-10-06)

- Added an optional **Add notes** action to every entity and collection heading. Notes remain absent from the inspector
  until requested, persist with the entity, reopen automatically when authored, and can be hidden without deleting the
  text.
- Replaced the outliner's hard-coded values with live, type-specific context for all 35 default entries. Examples include
  light output/type/position, precipitation type and mm/h, lunar phase angle and percentage, wind speed/bearing, camera
  focal length/aperture, cloud coverage/density, fog density/range, and geometry position.
- Values update with edits; names, hierarchy, visibility, status controls, inspector ordering and bake-last placement are
  unchanged. Light units follow their existing authored models: native Area uses lx, Point/Spot use cd, and illustrative
  source-flux panels use lm.
- Validation: the 212-check C053 regression set still passes, plus 6 entity-context checks covering all 35 entries and the
  collection workspace persistence checks. The generated standalone remains 4.64 MiB with 161 shipped icons and 57
  native vector glyphs.

## C056 — shared Fog visibility and embedded beam chamber (2026-10-06)

- Height Fog now keeps the preferred imported **Visibility** contrast-transmission card and removes the duplicate native
  visibility graph. Its Beam chamber is nested inside Visibility rather than occupying a separate card.
- Atmospheric Fog and Local Fog now use the same Visibility / Light transport card structure, while retaining their
  distinct models: aerial start-distance extinction for Atmospheric Fog and density × coverage for Local Fog. Their
  distance probes, saved values and native medium controls remain editable.
- Replaced Atmospheric Fog's landscape illustration with an abstract volumetric fog field: soft layered extinction bands,
  suspended samples, a fading light path and distance/contrast markers. It is explicitly an HTML authoring visual, not an
  environment render or native atmosphere march.
- Validation: 202 focused checks passed across inspector order, references, layout, fog/environment graphs, instrument
  styling, lighting, shared cards, cloud placement and the new five-check shared Fog-card audit. The generated standalone
  remains 4.64 MiB with 161 shipped icons and 57 native vector glyphs.

## C057 — one unified Fog-card family (2026-10-06)

- Replaced the mixed imported/native Fog implementations with one parameterized `FogPanel` used by Height Fog,
  Atmospheric Fog and Local Fog. All three now share the same abstract fog field, readings, Fog Settings, Visibility,
  embedded Light Transport / Beam Chamber, Medium and Wind Binding visual language and ordering.
- Height Fog no longer requests separate imported summary/detail iframes. The preferred richer Visibility treatment is
  implemented directly in the shared React component, so Fog inspectors cannot drift into parallel lookalikes.
- Beam Chamber remains inside Visibility's Light Transport section for every model. Its authored spread driver is Sun
  Scatter for Height Fog, Mie Blend for Atmospheric Fog and Anisotropy for Local Fog.
- Medium now contains one shared live parameter map for every Fog type while preserving the original editable controls.
  Model-specific technical cards remain: altitude density for Height, spectral transmission for Atmospheric, and bounded
  shape for Local. Notes, useful outliner metadata, persistence and independent model values remain available.
- Shared visuals use layered extinction bands, particles, a fading beam and distance markers rather than a landscape or
  environment scene. Small technical overlays communicate vertical falloff, atmospheric start distance or local bounds
  without changing the component architecture.
- Visibility calculations remain model-specific: altitude-adjusted exponential falloff for Height, extinction after Start
  Distance for Atmospheric, and density multiplied by coverage inside Local bounds. Atmospheric probes span 0–4000 m;
  Height and Local probes span 0–400 m.
- Verification: the standalone build succeeds at 4.64 MiB. **201 focused checks pass** across Fog cards, environment
  graphs, layout, inspector order, references, instrument styling, shared cards, lighting, Cloud placement and light design.
  Tests were updated to assert the unified native architecture instead of the superseded imported Height Fog path.
- Nine actual browser screenshots and a proof manifest are in `ProjectZeroEditor/Screenshots/UnifiedFog/`, covering the
  top, Visibility / Light Transport and Medium regions for Height, Atmospheric and Local Fog.

## C058 — Fog infographic card redesign (2026-10-06)

- Redesigned only the shared Fog card family, using the supplied dark dashboard and biomarker-card references: larger
  headline metrics, slim colour keys, rounded inset surfaces, micro status labels, compact trend plots and restrained
  model accents. No other inspector family was restyled.
- Kept one reused `FogPanel` implementation for Height, Atmospheric and Local Fog. The models still differ only in
  authored values, calculations, labels and technical content; no separate model-specific card implementation was added.
- Reworked the shared overview and six KPI readings into infographic cards. Visibility retains the shared interactive
  transmission plot, while Beam Chamber remains nested inside Visibility as a rounded Light Transport inset rather than
  becoming a standalone card.
- Fog Settings, Medium and each model's existing technical card retain their controls and now use the same rounded,
  keyed visual language. Notes, persistence, outliner context, Wind Binding and model independence are unchanged.
- The standalone HTML build succeeds at 4.65 MiB. Fog-specific computed-style assertions now cover the authorized
  18 px KPI cards and 17 px nested Beam Chamber treatment.

## C059 — Fog infographic correction: reuse existing cards (2026-10-06)

- Removed the additional Fog overview and six standalone KPI tile surfaces introduced in C058. The existing shared
  Visibility card is now the infographic surface; its six statistics are cells inside that card, not separate cards.
- Kept the existing Fog Settings, Visibility, Medium, model-specific technical and Wind Binding cards. No replacement
  card family and no per-model card variants were created.
- Visibility now follows the supplied dashboard references through one large live metric, a bordered statistics table,
  the existing transmission plot and restrained accent/status details. Beam Chamber remains an inset inside that same
  Visibility card. Medium keeps its original controls and live parameter map with only Fog-scoped visual styling.
- The correction remains shared by Height, Atmospheric and Local Fog through one `FogPanel`; only their data, labels,
  calculations and technical content differ. No non-Fog inspector styling changed.

## C060 — restore C054 Fog cards; Height-only visual correction (2026-10-06)

- Restored the Fog card implementation and styling from commit `9341425fd97992815e916537418b6b331648254d`.
  Atmospheric Fog and Local Fog are unchanged from that baseline.
- Changed Height Fog only: removed the later native `Visibility through fog` graph and retained the richer C054 imported
  Visibility/contrast visual in its place.
- Split the retained Height Fog Beam Chamber into its own reference slice and nested it inside the existing native
  Medium card, after Density, Falloff Height and Sun Scatter. It is no longer attached to Visibility and no new card was
  created.
- Height Fog retains its C054 summary visual/statistics, Fog Settings, Medium, Density with altitude, Colour and Wind
  Binding controls. Other inspectors are outside this correction.
- The standalone HTML build succeeds at 4.64 MiB. Focused tests now assert three Height reference slices (summary,
  Visibility and Beam), no duplicate native Visibility card, and Beam ownership by Medium.

## C061 — Height Fog shared variables and authored density visual (2026-10-06)

- Kept the C060 Height-only card arrangement: rich imported Visibility replaces the generic native visual and Beam Chamber
  remains inside Medium. Atmospheric Fog, Local Fog and non-Fog inspectors are unchanged.
- Added a single Height Fog property bridge for Enabled, Density, Falloff Height, Sun Scatter and Colour. Native Medium
  controls now repaint the imported summary, Visibility and Beam Chamber frames; edits made through the imported visual
  write back to those same native properties.
- Removed duplicate persistence for those mapped properties from `ReferenceInspector.Properties`. Native Height Fog values
  are authoritative; imported cards receive projections of the same values instead of maintaining a second copy. Existing
  saved scenes and newly imported scenes are normalized to remove the legacy duplicate keys.
- Beam Chamber now responds to every Medium property. Its representative 25 m layer uses Density and Falloff Height for
  extinction, Sun Scatter for spread, Colour for tint and Enabled for participation.
- Replaced the blank/flat Density with altitude graph with a non-graph layered-volume illustration. It remains visible as
  an authored preview when the medium is disabled and uses the same Density, Falloff Height, Sun Scatter, Colour and
  Enabled values as Medium and Beam Chamber. Redundant probe altitude/distance controls were removed from Height Fog.
- The standalone build succeeds at 4.65 MiB. Focused checks cover bidirectional value synchronization, Beam repainting for
  all Medium properties, removal of duplicate persisted properties, the non-graph density visual and unchanged C054 cards
  for Atmospheric and Local Fog.

## C062 — visible Beam preview and volumetric altitude chamber (2026-10-06)

- Height Fog only: Beam Chamber now remains visibly active as an authored preview while the runtime Enabled switch is off.
  Its display uses exposure-compressed physical transmission so dense authored fog remains readable, while the numeric
  120 m readout continues to report the uncompressed physical result. Live/Preview labeling makes the state explicit.
- Replaced the striped Density with altitude treatment with a non-graph volumetric chamber: a perspective-bounded fog
  volume, soft altitude layers, suspended particles, a light shaft, a falloff plane and an altitude direction cue. It uses
  the existing shared Height Fog variables and remains readable in disabled preview state.
- No controls, variables or cards were added. Atmospheric Fog, Local Fog and all non-Fog inspectors are unchanged.

## C063 — interactive altitude-density profile and explicit Beam response (2026-10-06)

- Height Fog only: replaced the C062 volume illustration with a dedicated interactive altitude-density profile. It is not
  a copy of Spectral transmission: the horizontal domain is altitude (0–3 km), the vertical domain is extinction density
  (0–0.2 m⁻¹), and its exponential profile follows Height Fog's density/falloff model.
- Dragging the profile point edits the existing Density and Falloff Height properties directly. No probe values or duplicate
  controls were introduced; Medium, Visibility, the altitude profile and Beam Chamber continue to share one property set.
- Clarified Beam Chamber within its retained Medium-card footprint. It now shows a readable scatter cone, center beam, physical 2% range marker,
  density/falloff/scatter readout, explicit LIVE/PREVIEW state and physical transmission at 120 m. Display exposure remains
  readable for dense fog and while disabled, while numeric labels retain physical values.
- Atmospheric Fog, Local Fog and non-Fog inspectors remain unchanged.

## C064 — Fog card correction and shared native-editor handoff (2026-10-06)

**Documentation-only checkpoint. No HTML, JavaScript, CSS, renderer or C++ source is changed by C064.**
This section records the required correction for the later C++ editor implementation and supersedes the visual direction
in C062 where it conflicts with C063/C064.

### Current approved Height Fog structure

- Preserve one authoritative Height Fog property set: **Enabled, Density, Falloff Height, Sun Scatter and Colour**.
  Every Height Fog control and visual must read/write that same set; do not persist iframe/reference duplicates.
- Preserve the existing card arrangement. The richer imported **Visibility** treatment replaces the generic native
  Visibility graph. **Beam Chamber remains an inset inside Medium**, below Density, Falloff Height and Sun Scatter;
  it must not become a standalone card.
- Preserve the C063 interactive **Density with altitude** profile. It is an altitude/extinction profile, not a copy of
  Atmospheric Fog's spectral-transmission treatment. Its horizontal domain is altitude (0–3 km), its vertical domain is
  extinction density (0–0.2 m⁻¹), and dragging its authored point edits the existing Density and Falloff Height values.
  It must not introduce probe fields, secondary density state or another falloff value.
- Keep model readouts truthful: datum density, density at one falloff height, density at twice falloff height, physical
  transmission at 120 m and the physical two-percent visibility range are derived outputs, not editable duplicate state.

### Required background correction

- Correct the **Beam Chamber** and **Density with altitude** surfaces so they belong to the same property-card family as
  Medium and the other inspector cards. Remove the disconnected near-black/black-box appearance.
- Use the editor's normal card surface (`#191919` in the HTML reference, or the corresponding native theme token) for
  the card/inset background. Plot/canvas rendering should be transparent or inherit that surface rather than painting an
  independent `#070809`/`#08090a` rectangle. Retain only subtle dividers/grid lines and the existing rounded geometry.
- Do not add another outer card or stack multiple dark shells. The Beam Chamber remains a nested inset in Medium; Density
  with altitude remains its existing technical card. The visual hierarchy must come from spacing, fine borders and text,
  not a substantially darker rectangle.

### Required Beam Chamber legibility and response

- The current Beam Chamber is too faint. Increase the minimum authored-preview exposure, beam-core contrast and scatter-
  cone visibility so the chamber remains immediately legible at the dense Height Fog values shown in review
  (`Density 0.1437 m⁻¹`, `Falloff Height 904 m`, `Sun Scatter 1.33`, white colour) and across the supported range.
- Disabled Height Fog must show an explicit **PREVIEW**, not an apparently broken blank chamber. Enabled Height Fog must
  show **LIVE**. Preview exposure is a presentation aid only; it must not be reported as physical transmission.
- Preserve the physical model and truthful labels. Density and Falloff Height determine extinction in the representative
  25 m layer; Sun Scatter clearly changes cone width/spread; Colour clearly changes beam tint; Enabled changes LIVE versus
  PREVIEW participation. The physical two-percent range marker and uncompressed 120 m transmission remain visible.
- Separate display exposure from physical values. A bounded exposure curve may keep dense fog readable, but changing each
  canonical property must produce an obvious visual response. Avoid a single dim line, source dot or imperceptible fade.
- The native implementation should test at least disabled/dense, enabled/dense, enabled/thin and non-white-colour states.
  Visual tests must verify meaningful pixel/geometry changes for all five canonical Height Fog properties, while numeric
  assertions independently verify the physical readouts.

### Apply the same corrected cards to all Fog entities

- After correcting Height Fog, update **Atmospheric Fog** and **Local Fog** to use the same card components, surface tokens,
  spacing, rounded inset treatment, status language and Beam Chamber legibility. Reuse one implementation; do not create
  three lookalike card families or copy independent control state.
- Keep the common ordering and ownership: header/identity, Fog Settings, Visibility, Medium with nested Beam Chamber,
  model-specific technical visual, Colour, Wind Binding, then any existing bake/export section at the bottom.
- Keep model semantics distinct while sharing presentation:
  - **Height Fog:** Density + Falloff Height drive altitude extinction; Sun Scatter drives beam spread; the technical card
    is the interactive altitude-density profile.
  - **Atmospheric Fog:** Density + Start Distance drive distance extinction; Mie Blend drives beam spread; retain its
    model-specific spectral-transmission technical content rather than relabelling the Height profile.
  - **Local Fog:** Density + Coverage/bounds drive local extinction; Anisotropy drives beam spread; retain editable local
    bounds and the model-specific bounded-volume technical content.
- Visibility, Medium, Beam Chamber and technical visuals for each entity must consume that entity's one canonical property
  set. Shared components must not cause Height, Atmospheric and Local Fog values to overwrite one another.
- Preserve useful entity-specific outliner values and units, Notes behavior, existing persistence, Height's imported
  Visibility replacement and Local Fog bounds. Do not change Clouds, Local Clouds or any non-Fog inspector as part of
  this correction.

### C++ editor conversion requirements

- Port behavior and information architecture, not browser implementation details. Use native theme tokens, native widgets,
  existing Fog owners and renderer-facing properties; do not embed the HTML iframe/state bridge in C++.
- Treat native scene properties as authoritative and calculate graph samples/readouts on demand with bounded iteration and
  no large automatic arrays. Dragging the altitude profile writes the native Density and Falloff Height properties through
  the normal undo/transaction path.
- The Beam Chamber may use exposure compression for its illustration, but physical transmission/range labels must be
  calculated from the uncompressed model. Label authored preview versus runtime participation explicitly.
- Verify expanded and narrow inspector widths, card background parity, clipping, scrolling and bottom-section ordering.
  Regression coverage must confirm Atmospheric Fog and Local Fog retain their distinct calculations and Local Fog retains
  its bounds after the shared-card migration.

**C064 status:** requirements recorded for implementation. The requested card/background/rollout correction is not claimed
as implemented by this documentation-only checkpoint.

## C065 — Fog surface correction and shared Medium Beam Chamber (2026-10-06)

- Implemented the C064 Fog-card correction in the HTML editor. Height Fog's interactive altitude-density plot and nested
  Beam Chamber now use the normal `#191919` card surface instead of disconnected near-black canvas rectangles.
- Increased Height Beam Chamber preview exposure, scatter-cone opacity and beam-core contrast. Dense/disabled authored
  settings remain visibly illustrative and labelled PREVIEW, while the two-percent range and 120 m transmission labels
  continue to report uncompressed physical values.
- Moved Atmospheric Fog and Local Fog Beam Chambers into their existing Medium cards, matching Height Fog ownership.
  Removed their former standalone Light transport sections; no new outer cards were added.
- Atmospheric and Local Fog now reuse one responsive Beam Chamber component and the same surface/status treatment. Their
  calculations remain separate: Atmospheric uses Density, Start and Mie Blend; Local uses Density, Coverage/bounds and
  Anisotropy. Disabled entities retain a visible authored preview and physical readouts remain uncompressed.
- Preserved Height Fog's imported Visibility replacement, canonical Height value bridge, Atmospheric spectral study, Local
  bounds, Fog Settings, Wind Binding, notes/persistence and every non-Fog inspector.

## C066 — Atmospheric Fog graph interaction while disabled (2026-10-06)

- Preserved the existing Spectral transmission and Visibility card designs; this correction changes interaction/model
  behavior only.
- Both graphs now remain responsive authored previews while Atmospheric Fog is disabled instead of collapsing to a flat
  100% line from forced zero extinction. The disabled status is explicitly labelled `AUTHORED PREVIEW · MEDIUM DISABLED`.
- Visibility distance is now the travelled distance inside the medium after the authored Start boundary. Dragging or using
  arrow/Home/End keys moves the probe and immediately changes its transmission readout and marker.
- Spectral transmission uses that same in-medium path and authored Density/Mie Blend while disabled. Dragging wavelength
  now traverses a visible spectral response rather than a flat line when the stored probe distance is before Start.
- Runtime participation is unchanged: disabling Atmospheric Fog still means zero extinction in the scene. Preview curves
  are editor-only authoring feedback, and the card copy distinguishes them from live behavior.
- Atmospheric and Local Beam Chambers use the same authored-density preview rule, while retaining their uncompressed
  physical labels and explicit LIVE/PREVIEW state.

## C067 — coordinated Fog menus using the native C++ categories (2026-10-06)

- Rearranged Height Fog, Atmospheric Fog and Local Fog into one shared card structure; this is an information-architecture
  correction, not a new visual-card design. All three now use the same React `Card`, `FogGraph` and `FogBeamChamber`
  implementations instead of combining imported Height frames with separate Atmospheric/Local instrument families.
- The shared order is now **Fog settings → Visibility through fog → Medium + model technical card → Wind binding**.
  Medium and its technical peer use the same two-column grid at expanded widths and the same stacked behavior when narrow.
- Categories and slider ownership follow `Frontier/Engine/Editor/FogInspectorPanel.cpp` and the extracted native sheets:
  - Height Fog: Fog settings = Enabled; Medium = Density, Falloff Height, Sun Scatter; Height and tint = interactive
    altitude-density profile plus Colour.
  - Atmospheric Fog: Fog settings = Enabled; Medium = Density, Start, Mie Blend; Spectral transmission remains the
    model-specific technical card.
  - Local Fog: Fog settings = Enabled and Follow Wind; Medium = Density, Coverage, Feature Scale, Anisotropy; Local bounds
    retains Centre and Half Size.
- Visibility through fog is now the same interactive card for all three models. It follows the C++ probe category: one
  diagnostic Distance control with analytic fog sampled at fixed world Z = 2 m; the former non-native Probe altitude input
  is removed. The superseded Height-only imported summary/Visibility/Beam frames are no longer mounted, removing the
  visual mismatch and duplicate Fog menu family.
- Beam Chamber remains inside Medium for every model and is now one shared component. It maps spread to Sun Scatter,
  Mie Blend or Anisotropy as appropriate; Height uses its authored Colour and 25 m layer/120 m range while Atmospheric and
  Local use their own extinction models and 400 m diagnostic range.
- Existing model values, units and limits still come from `NativePanels.json`; no slider range was invented or changed.
  Height's canonical-value cleanup remains in place for older saved/imported scenes.

## C068 — proposed native Fog-card conversion and Light components (plan only, 2026-10-06)

**Planning checkpoint only. C068 does not claim that the C++ editor, scene schema, renderer or creation menus have been
changed. Implementation must be reviewed against this plan before being described as complete.**

### Entity relationship decision

Atmospheric Fog, Height Fog and Local Fog remain independent sibling scene entities under Environment/Fog. Atmospheric
Fog is **not** made a child component of Atmosphere, and Height Fog is not embedded in Atmosphere either. Atmospheric Fog
consumes the selected atmosphere medium's Rayleigh/Mie coefficients, so its inspector should expose an **Atmosphere
source** reference/readout, but ownership and lifetime remain separate. This avoids deleting Fog when an Atmosphere is
replaced, permits Fog to be disabled independently, and supports scenes with multiple atmosphere assets. A Fog collection
folder may group the rows visually without changing component ownership.

### Native Fog inspector: cards to retain, replace, add and remove

The source of truth is the coordinated C067 HTML layout plus the real property ranges/groups built in
`CelestialSequence.cpp`; the implementation target is `FogInspectorPanel.cpp`.

| Action | Native card/component | Planned result |
|---|---|---|
| Retain and share | Fog settings | Same card shell for all models. Height/Atmospheric: Enabled. Local: Enabled + Follow Wind. |
| Replace presentation, retain model | Visibility through fog | One shared interactive native card for all three. One UI-session Distance probe; analytic Fog samples fixed world Z = 2 m. No persisted Probe altitude property. |
| Retain and share | Medium | Same card and control renderer; labels/ranges come from the selected native sheet, never a second hard-coded property model. |
| Add inside Medium | Beam Chamber | Shared native drawing. Height maps Sun Scatter/Colour and a 25 m layer; Atmospheric maps Mie Blend; Local maps Anisotropy. LIVE/PREVIEW is explicit and physical labels remain uncompressed. |
| Replace | Height technical card | Rename to **Height and tint**; interactive altitude-density profile edits native Density/Falloff Height through the normal transaction path, with native Colour in the same card. |
| Replace | Atmospheric technical card | **Spectral transmission** becomes an interactive wavelength study driven by native Density/Mie Blend and the shared Distance probe; include Atmosphere source/coefficient readouts. |
| Retain | Local bounds | Keep Centre and Half Size controls and the bounded-volume drawing; use the same outer card geometry as the other technical cards. |
| Move/rebuild | Wind binding | Stop drawing raw `RecordWindBindingControls` before the custom panel. Render one normal **Wind binding** card at the bottom from the existing Own Wind/Wind Source properties. |
| Remove as standalone | Neutral-light reference | Fold any useful neutral-target swatch/readout into the model-specific technical card; do not keep an extra card that exists in only the native menu. |
| Remove | Per-model card shells and duplicate probes | One shared Fog card renderer with model descriptors; no Height/Aerial/Local lookalike implementations and no duplicated scene state. |

Native Fog order will be: header/identity → Fog settings → Visibility through fog → two-column Medium + model technical
card (stacked when narrow) → Wind binding. Expanded and narrow layouts must preserve that order.

### Native Light component architecture

The current native state is not sufficient for the HTML Light inspectors: `PunctualLuminaireRecord` stores only
Directional/Point/Spot records and is explicitly not consumed by the lighting kernel; the generic editor sheet exposes
Intensity/Colour plus a read-only Direction; and the viewport Point/Spot/Area creation menu entries are no-ops. The plan
will not present those paths as working Light components until persistence, writeback and renderer ownership are real.

Add one persistent **LightComponent** schema with shared fields (enabled, type, colour mode/value, output, range, shadow
policy, transform and optional distribution/profile reference) and type-specific payloads. Use a type/distribution enum,
not one C++ class per marketing preset:

- Core native types: **Point**, **Spot**, **Directional**, **Rectangle/Area**, **Tube**, and **Strip**.
- Optional distribution: uniform, IES profile, or automotive low-beam profile.
- HTML names become creation presets over those components:
  - Key Spot → Spot;
  - Rim Point and Fill Point → Point;
  - ECE Low Beam → Spot + automotive distribution;
  - Softbox → Rectangle/Area;
  - Studio Tube → Tube;
  - LED Emitter → compact Point/Area preset;
  - LED Strip → Strip with one horizontal direction;
  - IES Downlight → Spot + IES profile.
- Imported KHR punctual lights adapt to the same component schema rather than maintaining a second inspector model.

Creation commands must replace the no-op Viewport menu items, create a real component and outliner row, select it, and
support save/reload. Presets remain data/defaults; selecting a preset must not switch to a separate inspector code path.

### Shared native Light inspector cards

The native inspector will use one `EditorSheetAppearance::Light` route and one Light card family. Type descriptors decide
which rows are present; all common cards remain visually identical.

| Order | Card | Controls/content | Action relative to current native editor |
|---:|---|---|---|
| 1 | Main light visual/data | Existing approved type-specific source drawing plus real output/type/position summary. No copied identity strip. | Add dedicated native card; replace generic-only presentation. |
| 2 | Statistics | Real component type, output unit, range, dimensions/profile and world XYZ. | Add; no synthetic renderer telemetry. |
| 3 | Quick controls | Enabled, Cast Shadows and only other genuinely wired booleans. | Add shared card. |
| 4 | Transform | Standard native Position/Rotation/Scale controls and transaction/undo behavior. | Replace read-only Direction-only treatment; reuse standard Transform. |
| 5 | Source & response | Colour/temperature mode, output, range/falloff and type dimensions in one card with the approved data-oriented response study. | Replace separate/duplicated source, colour and distance-response sections. |
| 6 | Distribution | Spot cone/penumbra, IES/profile selection, automotive cutoff, or area/tube/strip emission shape as applicable. Omit when not applicable. | Add conditional shared card, not preset-specific cards. |
| 7 | Dynamics | Existing real flicker/pulse controls only when backed by component state; otherwise omit the card. | Do not add decorative controls. |
| 8 | Baking/export | Existing real bake/export actions and honest unsupported states. | Move/keep at inspector bottom. |

The Area Light card keeps the corrected area-source drawing and standard Transform. LED Strip remains one horizontal
emission direction. Point/Spot use cd, directional/illuminance paths use lx, and authored source-flux panels use lm only
where the underlying component actually stores lumens; unit conversion must not be implied by relabelling.

### Implementation sequence

1. Freeze the native property/schema contract and migration for existing punctual/emissive scene data.
2. Refactor `FogInspectorPanel.cpp` around shared card helpers/model descriptors; move Wind binding and add the shared Beam
   Chamber without changing Fog equations.
3. Add the Light component record, stable IDs, scene serialization, outliner rows, selection, creation commands and native
   sheet/writeback path.
4. Add the shared native Light inspector and type-conditional cards. Presets create configured core components.
5. Connect runtime lighting incrementally. Point/Spot/Directional may be called live only after the renderer consumes the
   edited records. Area/Tube/Strip/IES/ECE paths must show an explicit authoring/unsupported state until their actual
   renderer path exists; no fake preview may be reported as scene lighting.
6. Add undo/redo and save/reload verification for every editable field and create/delete operation.
7. Run native Linux and Windows/MSVC builds, existing renderer/editor regression suites, low-stack checks, expanded/narrow
   ImGui captures, and interaction tests using submitted ImGui items. Verify Fog equations and model independence, Light
   units, preset-to-core mappings, card order, clipping, scrolling and baking-last placement.

### Completion gates

- All three native Fog inspectors have identical card shells/order and only model-appropriate controls.
- Fog edits reach the existing real CPU/GPU Fog owners; preview exposure never changes physical readouts.
- Every Light row owns or adapts to a real persistent component; no creation menu entry is a no-op.
- Every displayed Light control writes through and survives save/reload; unsupported renderer features are labelled.
- Existing Atmosphere remains an independent source entity; Fog references it without ownership coupling.
- No changes to Clouds, materials, fracture, GI or unrelated inspectors are included in this conversion.

## C069 — complete HTML-to-native inspector-card audit (source comparison, 2026-10-06)

**Audit/documentation checkpoint only. No C++ or HTML implementation is changed by C069.** The earlier C068 plan was too
narrow because it treated Fog and Lights as the only conversion work. This audit compares the complete current HTML
inspector order with every dedicated native inspector route and the generic native fallback.

### Evidence and comparison method

- Current HTML order contract: `Frontier/Experimental/ProjectZeroEditor/CheckInspectorOrder.mjs`, plus the shared Fog
  implementation in `Inspectors.jsx` and the Light/Cloud reference adapters.
- Native dispatch: `Frontier/Engine/Editor/InspectorPanel.cpp` and `EditorSheetAppearance` in `EditorInstance.h`.
- Native card implementations: `AtmosphereSkyInspectorPanel.cpp`, `CameraInspectorPanel.cpp`,
  `CloudsInspectorPanel.cpp`, `FogInspectorPanel.cpp`, `LensFlareInspectorPanel.cpp`, `MoonInspectorPanel.cpp`,
  `StarsInspectorPanel.cpp`, `SunInspectorPanel.cpp`, and `WeatherInspectorPanel.cpp`.
- Generic native sheet/card source: `EditorFeedSequence.cpp` and `CelestialSequence.cpp`.
- This is a code-level/card-order comparison. Existing native captures are linked below where available; C069 did not
  rebuild or rerender the native proofs and therefore does not claim a new image comparison.

Status legend: **Aligned** = equivalent native card family/order and real bindings exist; **Partial** = native behavior
exists but presentation/order/content differs; **Missing** = HTML-approved card/component has no dedicated native route.

### Complete parity matrix

| Inspector family | Current approved HTML cards/order | Current C++ cards/order | Status and difference to resolve |
|---|---|---|---|
| Folder / collection | Collection total → visibility status → composition → searchable browser → optional Notes | Native collection inspector, search/filter/paging and Notes from C021/C055-era native work | **Partial/close.** Recheck optional add/reveal Notes behavior and the latest entity-specific outliner metadata; do not replace the native collection implementation. |
| Geometry | Standard Transform → capabilities → Material → Fracture/SDF last | Generic Transform/Surface property groups; special Tyre/SolidArc routes exist, but the HTML fracture/SDF authoring stack is not a general native geometry card | **Missing/partial.** Retain standard native Transform and Material; port Fracture only through its separately approved native scope. Do not fold Tyre/SolidArc into generic geometry. |
| Camera | Lens + field of view → Aperture study → Subject plane/sharpness → Sensor/support | Same four dedicated cards in `CameraInspectorPanel.cpp` | **Aligned functionally.** Compare spacing, responsive pairing and card heights; no replacement architecture required. |
| Post process | Quick/capability controls and data graphs; no invented preview/statistics | Generic Exposure → Tone Mapping → Lens Effects sheet groups | **Partial.** Add a dedicated shared card presentation only if needed; preserve real EV, saturation, contrast, bloom and vignette fields and do not fabricate renderer statistics. |
| Atmosphere | Atmosphere Lab/main data → settings → scattering → native controls → Atmosphere bake → Baked atmosphere | Atmospheric scattering → Aerosol haze + Ozone → Density falloff → Ground reflectance → additional settings → bake/baked at bottom | **Partial/close.** Native C024 has real interactive curves and bottom baking. Missing HTML AtmosphereLab atlas/orbital presentation remains a separate decision; do not duplicate the existing native scattering controls. |
| Sun | Main solar visual/readings → quick controls → authored properties/graphs → Sun lighting/disk baking last | Sun direction + intensity/temperature → Daylight cycle + Sun disc → Dynamic/additional settings → both bake cards last | **Partial/close.** Native C024 has real solver-backed graphs and correct bake-last order. Reconcile the HTML summary/readings and quick-control boundary without replacing native solver controls. |
| Lens flare | Flare composite → switches/settings → source → legacy settings → Lens flare image/bake last | Lens flare image first → Flare composite → Flare layers + Lens ghosts → Halo/response → Legacy settings | **Order mismatch.** Move the native image/export card to the bottom, retain composite/layer/ghost/halo controls, and group switches below the composite. |
| Moon | Lunar phase first → Moon settings → catalogue → Moonlight/size/position/rotation | Moon settings → catalogue → Lunar phase → Moonlight + size → position + rotation | **Order mismatch.** Reorder native Phase before settings/catalogue while retaining the real atlas, four-slot state, follow-sky, roll/pitch and oversized-moon behavior. No imported HTML Atlas frame should be embedded. |
| Stars | Star field first → Stars settings → Twinkle → Renderer scale → Star field bake last | Stars settings → Star field → Twinkle + Celestial rotation → Renderer scale → Baking/star atlas last | **Order mismatch.** Move Star field ahead of quick settings; retain native rotation and honest unavailable bake state. |
| Wind | Anemometer/statistics → Composite Flow/Wind field → Wind controls | Direction + magnitude + Flow → Variation + Gust envelope; wind binding is drawn separately for consumers | **Major presentation gap.** Keep the native `WindField` data/solver, add the reviewed Anemometer first, then composite field, then controls. Remove neither Beaufort/variation behavior nor real wind ownership; avoid a browser iframe port. |
| Height / Atmospheric / Local Fog | Shared Fog settings → shared interactive Visibility → Medium with shared Beam Chamber + model technical peer → Wind binding | Shared native Fog shell already has Settings → Visibility → Medium + technical peer, but lacks coordinated Beam ownership, has standalone Neutral-light reference, and draws Wind binding before the custom panel | **Partial; C068 work remains.** Add Beam inside Medium, move Wind binding to bottom, fold/remove standalone neutral card, preserve independent Fog entities and equations. |
| Global / Local Clouds | Shared summary/statistics → Cloud settings → shared Coverage → Cloud base/deck; Local retains bounds-derived base/thickness and body | Settings → Coverage → Cloud base/Local bounds + Layer thickness/Volume section → Cloud body → global shadow foldout | **Partial.** Native has the correct real fields and shared global/local route, but not the approved summary/deck presentation/order. Preserve Local Centre/Half Size and global-only shadow controls; do not invent Local Base/Thickness state. |
| Precipitation | Precipitation type → Emission + collision → Fall + density → remaining simulation cards | Emission + collision → Precipitation type → Fall + density + Particle scale → Simulation/settling | **Order mismatch.** Move native type first, then emission, retaining real particle-scale and simulation/settling cards. |
| Rainbow | Optical preview → Visibility → Bow response → Bake/image last | Visibility → Optical preview → Bow response → Baking/image last | **Order mismatch.** Swap only the first two cards; retain the real CPU optical preview, authored response controls and explicit unsupported bake state. |
| Lights | Shared source-family dashboards, statistics, quick controls, standard Transform, merged Source & response, conditional distribution/dynamics, baking last | No dedicated Light appearance. Generic Light sheet exposes Intensity/Colour and read-only Direction; Point/Spot/Area Construct menu entries are no-ops; punctual records are not consumed by the lighting kernel | **Missing.** C068 Light component/schema/creation/writeback/renderer plan remains required. Presets map to core Point/Spot/Area/Tube/Strip types rather than separate inspector classes. |
| Materials / Shader editing | Material channels/source editor and dockable ShaderEditor | Generic Surface card exposes Albedo, Emission, Roughness and Metallic readout | **Missing/partial.** Keep real native material properties; port the approved source/channel workflow only after native material ownership and shader compilation contracts are defined. |
| Entity Notes and outliner context | Optional add/reveal Notes on every entity; type-specific values/units in outliner | Native `EditorInstance::Notes` and `RecordNotes` exist; outliner derives native categories/standing and selected metadata | **Partial/verify.** Audit reveal behavior and every latest metadata example (light output/type/XYZ, precipitation mode, Moon phase angle) rather than assuming C055 parity. |
| Diagnostics / collection workspace | Movable diagnostics and large-collection browser | Native C021/C022 implementations and proofs exist | **Aligned for the scoped native implementation.** Keep actual native timing semantics and do not replace with HTML browser timing. |
| Construct / creation | HTML palette includes all reviewed light presets and environment entries | Native Construct exists, but Point/Spot/Area menu callbacks are currently empty | **Missing for Lights.** Replace no-op commands with real component creation; do not add palette-only rows. |

### Cards/components to retain without redesign

- Native Camera cards and optics calculations.
- Native Atmosphere and Sun solver-backed interactive curves, additional native controls and bake-last sections.
- Native Moon atlas/data ownership, Cloud bounds/body/shadows, precipitation simulation/settling, Rainbow CPU preview,
  Wind solver fields, Fog equations, standard Transform, Notes storage and native diagnostics.
- Existing Tyre and SolidArc specialized routes remain separate from general Project-Zero card conversion.

### Cards/components to rearrange only

- Lens Flare: image/export to bottom; composite and switches first.
- Moon: Lunar phase before settings/catalogue.
- Stars: Star field before settings; baking remains last.
- Precipitation: type before emission/collision.
- Rainbow: optical preview before visibility.
- Clouds: apply the same approved summary/settings/coverage/deck ordering to Global and Local while retaining Local bounds.
- Fog: use the C067 shared order in native code, with model-specific technical peers.

### Cards/components requiring new native implementation

- Shared Fog Beam Chamber and coordinated bottom Wind binding.
- Wind Anemometer and approved composite-field presentation over the existing native wind model.
- Persistent Light components, creation, dedicated inspector, writeback, save/reload and honest renderer support.
- General geometry Fracture/SDF cards only within the separately authorized native fracture plan.
- Material channel/source editor and ShaderEditor parity.
- Any missing outliner metadata/reveal behavior identified by the C055 audit.

### Shared native card-kit correction

The dedicated C++ inspectors currently repeat local `Panel`/card drawing helpers. Before converting more families, extract
or standardize a native inspector card kit for: card surface/radius, heading, metric/readout, tile, slider host, responsive
pair/stack layout, technical plot, status text and terminal bake section. Model code continues to own calculations and
writeback. This prevents Fog, Cloud, Moon, Weather and future Light cards from becoming visually different while merely
sharing similar names.

The card kit is presentation infrastructure, not a second property model. Every control must still bind to the existing
`EditorSheet` property or a newly approved persistent component field through normal native transactions.

### Existing native visual evidence

These are previously committed native ImGui command rasterizations, not new C069 renders and not Vulkan-window captures:

- [Sun graphs](../../VisualProof/ProjectZeroNative/SunGraphs.png)
- [Sun narrow](../../VisualProof/ProjectZeroNative/SunGraphsNarrow.png)
- [Atmosphere graphs](../../VisualProof/ProjectZeroNative/AtmosphereGraphs.png)
- [Atmosphere narrow](../../VisualProof/ProjectZeroNative/AtmosphereGraphsNarrow.png)
- [Lens Flare](../../VisualProof/ProjectZeroNative/LensFlare.png)
- [Stars](../../VisualProof/ProjectZeroNative/Stars.png)
- [Rainbow](../../VisualProof/ProjectZeroNative/Rainbow.png)
- [Collection](../../VisualProof/ProjectZeroNative/Collection.png)
- [Diagnostics](../../VisualProof/ProjectZeroNative/Diagnostics.png)

Representative approved HTML evidence for direct review:

- [Current inspector order captures](../Experimental/ProjectZeroEditor/Screenshots/InspectorOrder/)
- [Global/Local Cloud layouts](../Experimental/ProjectZeroEditor/Screenshots/SharedCloudLighting/)
- [Light family layouts](../Experimental/ProjectZeroEditor/Screenshots/LightDesign/)
- [Fog cards](../Experimental/ProjectZeroEditor/Screenshots/FogCards/)

### Revised native conversion order

1. Establish the shared native card kit and a source-driven card-order test.
2. Apply rearrangement-only corrections to Lens Flare, Moon, Stars, Precipitation and Rainbow.
3. Reconcile Global/Local Cloud ordering without changing bounds or simulation ownership.
4. Implement the complete Fog conversion from C068/C067.
5. Implement Wind presentation over the real native Wind model.
6. Implement persistent Light components and the shared Light inspector; remove no-op creation commands.
7. Audit Folder/Notes/outliner metadata and Post Process.
8. Handle general Geometry Fracture/SDF and Material/ShaderEditor only under their separate authorized native plans.
9. Re-run Linux and Windows/MSVC builds, low-stack checks, native writeback/save-reload tests, expanded/narrow captures
   and existing renderer regressions. A card is not complete merely because a CPU-rasterized screenshot exists.

**C069 conclusion:** Fog and Lights are not the only outstanding conversion. Camera is substantially aligned;
Sun/Atmosphere and native workspace cards are close or scoped-complete; the remaining environment families have explicit
order/presentation differences, while Wind, Lights, general Fracture and Material/Shader workflows require larger native
work. No implementation or fresh native rendering is claimed in this audit.

## C070 — native Fog conversion and rearrangement parity tranche (2026-10-06)

This implementation checkpoint applies the first two C069 stages without replacing the native property model.

### HTML-to-C++ result

| Family | Approved HTML order | Native C++ result | Deliberate native difference |
|---|---|---|---|
| Height / Atmospheric / Local Fog | Fog settings → Visibility → Medium with Beam Chamber + technical peer → Wind binding | The shared `FogInspectorPanel.cpp` now uses that order for all three sibling entities. Beam is inside Medium, disabled entities show an authored `PREVIEW`, Atmospheric retains an interactive 380–780 nm response, and Wind binding is terminal. | The Beam is an analytic authoring diagnostic rather than a scene-camera image. Height samples its 25 m layer; Aerial begins after authored Start; Local uses the bounded volume model. |
| Lens Flare | Composite/switches first → image/export last | Confirmed native order is composite → layers/ghosts → halo → legacy → image/export. | Native retains real cached linear pixels and PFM export; baked scene playback remains explicitly unavailable. |
| Moon | Lunar phase first → settings → catalogue → lighting/size/pose | Reordered native card coordinates and proof interactions to exactly this sequence. | Native retains the six-body registered atlas, four independent slots, follow-sky protection and unrestricted typed angular size; no duplicate HTML atlas frame was imported. |
| Stars | Star field first → settings → Twinkle/rotation → renderer scale → bake last | Reordered field before quick settings while preserving all later cards and the terminal unavailable bake card. | Native field uses the real catalogue and sidereal/rotation controls rather than an HTML-only preview dataset. |
| Precipitation | Type → emission/collision → fall/density → simulation | Reordered Type ahead of Emission; retained particle-scale and simulation/settling cards. | Native telemetry remains backed by the CPU particle simulation and does not claim verified viewport particles. |
| Rainbow | Optical preview → visibility → bow response → bake last | Swapped only the first two cards; response and unavailable bake state remain unchanged. | Native preview uses the shared CPU spectral kernel under stated fixed test conditions, not the scene camera. |

### Proof and infrastructure corrections

- Fog native proof passes **128 checks**, including shared terminal Wind ordering, narrow layouts and disabled Local Fog preview.
- Moon passes **146 checks**, Stars **157**, Weather **1684**, and Lens Flare **74** after updating coordinate-sensitive interactions for the approved card moves.
- The native proof staging scripts now source `CameraInspectorBinding.h` from `Engine/Host` and include the shared Curve, environment-projection and wind-binding headers.
- Obsolete proof expectations for the compact 208-byte `PostConstantRecord` were corrected while retaining the asserted `PostLayers` and `PostStarEffects` offsets.
- Weather Wind now treats a missing staged `Air shear` property as unavailable instead of dereferencing null; the current application model still supplies and writes the property.
- Full prerequisite execution still stops in the pre-existing Sun interaction assertion `##sun-diameter`; all changed inspector translation units compile with `-Wall -Wextra -Werror`, and their dedicated proofs above pass.

Generated native captures were inspected for wide and narrow Fog, Moon, Stars, Precipitation and Rainbow, plus Lens Flare. They preserve the accepted card designs; this tranche changes ownership/order and diagnostic wiring only.

## C071 — Clouds, Wind, Light components, Notes and Post Process native completion (2026-10-06)

This checkpoint completes the remaining shared inspector work that can be landed without replacing the separately owned
geometry/material authoring systems.

### Implemented

- **Global and Local Clouds:** both now begin with the approved shared summary, then Cloud settings, interactive Coverage,
  deck/bounds technical cards and Cloud body. Local base/depth are derived from Centre/Half Size; no duplicate local state
  was introduced. Global-only shadow ownership remains unchanged.
- **Wind:** the native order is Anemometer → Composite wind field + Wind controls → Variation controls + Gust envelope.
  Every editable value remains the existing `WindField` property and the diagnostics call the real wind model.
- **Lights:** extended the existing scene luminaire component rather than creating a second property model. Core types are
  Directional, Point, Spot, Rectangle, Tube and Strip; distribution is Uniform, IES or automotive low beam. Components own
  enablement, shadows, colour, output, range, cone, dimensions and transform. Imported punctual rows and editor-created
  lights use the same inspector route.
- **Creation:** native Point, Spot, Directional and Rectangle viewport menu entries now call `ConstructEntity`; the core
  construction API also supports Tube and Strip. A successful creation refreshes the outliner. The construct proof creates
  all six light types and verifies stable placement/component attachment.
- **Light writeback:** `EditorFeedSequence` now builds and applies a dedicated Light sheet. Intensity, range, type,
  distribution, colour, cone, dimensions, enabled/shadow state and world position survive inspector rebuilds. Outliner
  metadata reports the real type, output and unit.
- **Light presentation:** one shared native inspector supplies Main visual/data → Statistics → Quick controls → Transform →
  Source & response → conditional Distribution → terminal Baking/renderer status. Spot/Point/Directional are labelled as
  native scene sources. Rectangle/Tube/Strip explicitly say `AUTHORING ONLY`; no unsupported lighting result is claimed.
- **Post Process:** EV, saturation, contrast, bloom and vignette now have project-owned session writeback and a dedicated
  native presentation. Its tone curve is derived from authored contrast and it deliberately contains no fake histogram,
  camera preview or renderer statistics.
- **Notes:** the existing optional Notes control is now appended to dedicated inspector routes as well as generic entities
  and folders, so dedicated Fog, Cloud, Weather, Camera, Light and celestial panels no longer bypass entity notes.
- **Outliner audit:** Light rows now expose type/output/unit and enabled standing. Existing native precipitation and Moon
  metadata remain model-derived rather than copied HTML strings.

### Renderer and persistence boundary

`PunctualLuminaireRecord` is now the persistent in-scene Light component and the native sheet/writeback path is verified.
The existing file-import adapters continue to populate that record. The current renderer's triangle-emitter sampler still
does not consume extended Rectangle/Tube/Strip components, and no lightmap bake path exists, so those states are visibly
reported as unsupported rather than represented by fabricated images. The component proof verifies reconstruction from the
scene record (the editor save/rebuild seam); this checkpoint does not claim a new `.space` project-writer workflow that the
runtime does not currently expose.

### Executed proof

- Clouds: **582 checks**.
- Wind/Precipitation/Rainbow: **1684 checks**.
- Existing C070 proofs rerun: Fog **128**, Lens Flare **74**, Moon **146**, Stars **157**.
- Light/Post presentation: **7 captures/checks**; component creation/writeback/rebuild/Post persistence: **11 checks**.
- Construct: all **15 catalogue entities**, including six persistent light types, with 1,936 real scene triangles and
  stable attachment checks.
- Changed C++ inspector/model/feed sources compile with `-Wall -Wextra -Werror`; the complete `FrontierRuntime.cpp` syntax
  path also compiles with the construction-world exchange enabled (the runtime retains its pre-existing unused local warning).

Visual index: [`VisualProof/NativeInspectorCards/index.html`](../VisualProof/NativeInspectorCards/index.html).

### Separate native plans retained

General Geometry Fracture/SDF and Material/ShaderEditor remain governed by their separately approved native plans. C071
keeps the standard Transform/Surface model and existing specialized SolidArc/Tyre routes intact; it does not disguise the
browser-only fracture or shader workspace as completed native editor functionality.

### C071 HTML reference versus native C++ audit

| Family | HTML/reference presentation | Native C++ result and intentional difference |
|---|---|---|
| Global / Local Clouds | Summary-led density/deck card, settings and coverage; Local includes an authored volume | Same hierarchy and visual language. Native derives Local base/top from `Centre ± Half Size`, retains `TextureCloudInspectorPanel`/simulation ownership, and omits any browser-only decorative value that is not in `EditorSheet`. |
| Wind | Anemometer readout, vector field, speed/direction and gust controls | Same order and card design. Native diagnostics use `WindField::QueryVelocity`; speed, direction, variability, frequency, gust, turbulence and shear all write to canonical properties. No Beaufort/steadiness/driving duplicate model was added. |
| Point / Spot / Area / Strip lights | Main light visual, compact statistics, quick toggles, transform, source response, conditional distribution, terminal renderer/bake status | Same accepted ordering/grouping. Native cards read `PunctualLuminaireRecord` and placement transforms. The HTML-style appearance is retained, while support labels are stricter: extended emitters say `AUTHORING ONLY`, because native raster consumption is not implemented. Strip keeps horizontal direction and Area keeps source/shape controls together. |
| Post Process | Exposure hero, tone response curve and lens effects; some reference designs implied analytics/preview panels | Native keeps the accepted three-card composition and real controls only. The curve is computed from contrast. Histogram, camera preview and renderer statistics were removed because no native source provides them. |
| Notes / outliner | Editable per-entity Notes and informative identity/meta text | Native Notes now appear after every dedicated panel, keep a stable inspector identity across construction-driven roster refresh, and remain session metadata. Light meta is generated from real type/output/unit rather than copied text. |

The visual proof deliberately presents native captures rather than side-by-side browser screenshots: it establishes the shipped
C++ result, while this table records every intentional data/feature difference from the HTML reference.

## C072 — exact LED Strip and Folder card correction (2026-10-06)

The first C071 native proof used a generic line for LED Strip and a generic ImGui folder summary. Those were not faithful ports and are replaced here.

- **LED Strip:** the native inspector now ports the accepted Ribbon light instrument rather than substituting a line glyph. It includes the elliptical 120-point emitter study, authored total flux, connected-load and emitter-count metrics, Output per metre, Electrical budget, Layout & segments, Opal diffuser, horizontal transform direction, scene participation and honest terminal renderer status.
- The strip values are canonical component fields on `PunctualLuminaireRecord`: flux/load per metre, dimmer, colour temperature, emitter density, voltage, diffuser and length. Build/apply/rebuild tests prove real writeback; total flux is derived as `lm/m × length × dimmer`.
- **Folder / Collection:** the native route now follows the accepted hierarchy exactly: collection heading and optional notes, ancestry trail, Collection contents, Visible/Hidden status, Composition distribution, then the searchable/filterable/sortable manifest. Result rows retain native selection and visibility writeback. The copied identity strip and the previous generic summary/browser styling are removed.
- Wide and narrow native CPU captures are included in `VisualProof/NativeInspectorCards/index.html`; the old simplified LED image is replaced.

Executed checks: changed C++ sources compile with `-Wall -Wextra -Werror`; Light presentation passes 7 captures/checks; Light component persistence passes 17 checks; Folder wide/narrow native capture proof passes.
