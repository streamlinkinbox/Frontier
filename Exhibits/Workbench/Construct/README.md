# Construct — native CPU authoring bridge

**Native C++ editor panel:** see [Native.md](Native.md) for the newer EditorHost
integration. This document describes the separate browser/CPU creation bridge.

## What is implemented

The SolidArc-inspired floating catalogue is available from the outliner **+** or
**Shift+A**, without adding a viewport. Categories, search, dimensional glyphs,
name/position/size, keep-open, keyboard dismissal, focus trapping and automatic
outliner selection are implemented.

Creation does **not** append a fake React entity first:

```
Construct menu → same-origin POST /api/construct → native WorldHost
 → ConstructEntity(SceneStructure&, request)
 → RegisterPlacement + RegisterInstance / RegisterCamera
 → Finalise (materials, flattened triangles, emission alias table)
 → persist command journal atomically → return authoritative scene snapshot
 → project placements into the browser outliner → select new placement
```

Supported entries: Cube, Sphere, Cylinder, Cone, Plane, Torus, Area emitter,
Camera, Empty entity. Meshes use metres and Z-up; area geometry faces downward
and emits 1,000 nits. Geometry has valid outward winding and no degenerate
latitude-pole triangles. IDs are append-only placement indices, **not outliner
row ordinals**. Names are made unique in the native scene.

Rejected native requests produce no outliner entry. A failed durable write
reconstructs the native world from the previous successful journal. Browser
reload and worker restart retain placements and identities. The journal is
`.frontier/construct-world.json`; it is local authoring data, intentionally not
committed. Existing browser-only project data is left intact and is not silently
claimed to have been imported into the native scene.

## Scope / important unfinished integration

This is a **native CPU authoring worker**, not the running Windows/Vulkan game
process. The browser projects its separate scene under **Native authoring scene**.
Existing browser environment demo rows are not this worker's world. The new
entity inspector is read-only; its drawing is a catalogue glyph, not a rendered
engine capture. Native entity visibility, transform editing, deletion/undo,
scene-file export and the live game-process transport remain unfinished.

`ConstructEntity` can operate on a caller's actual `SceneStructure`, but it is
**not yet wired into `GameExecution.cpp`, `EditorHost` or `RenderScheduler`**.
A production game integration must drain commands at a safe frame boundary,
rebuild traversal and GPU scene resources, preserve live motion/physics state,
refresh selection/outliner identity mappings and invalidate accumulation.
Calling the helper from inside ImGui drawing would be unsafe. None of those
GPU integration steps is represented as completed by these tests.

Punctual lights are deliberately excluded: the pinned engine stores their
records but does not light from them. Water/fluids are also excluded as requested.
This change does not modify Candy paint; the user's requested glitter/flakes
correction remains outstanding.

## Run

Requires `gh` access to the pinned engine dependency repository and a C++20 g++.
The builder verifies each downloaded source against its Git blob hash. It stages
only in `.cache/construct`; no branch changes or source resets are performed.

```sh
python3 Exhibits/Workbench/Construct/Build.py
python3 Exhibits/Workbench/Construct/Serve.py   # port 5191
npm --prefix Experimental/FrontierEditor install
npm --prefix Experimental/FrontierEditor run dev -- --port 5173
```

Vite proxies `/api/construct` to the native service. Browser code uses relative
URLs; it never addresses the user's localhost. Both servers bind to `0.0.0.0`.
A static `dist` deployment additionally needs this API proxy/service; without it
creation reports an error rather than silently creating browser-only objects.

## Verification

```sh
python3 Exhibits/Workbench/Construct/Build.py --sanitize
python3 Exhibits/Workbench/Construct/TestBridge.py
npm --prefix Experimental/FrontierEditor run build
```

Native regular and ASan/UBSan runs verify all nine entries, 1,936 real engine
triangles, instance attachments, camera attachment, nonzero emitter power,
normal/winding consistency, stable IDs, unique naming and invalid-input rejection.
Bridge tests verify replay persistence and injected disk-failure rollback.

The browser proof (Playwright + Chromium installed under `.cache/browser`)
verifies actual input, native response, outliner selection, reload, shortcut,
search, Escape, mobile width and no phantom row on API failure. It adds a named
proof cube to the running authoring scene; use an isolated `--scene` when needed.
The Sparticuz package includes `al2023.tar.br`; extract its `lib` directory and
add it to `LD_LIBRARY_PATH` if the host lacks NSS/NSPR libraries.

```sh
npm install --prefix .cache/browser playwright-core@1.63.0 @sparticuz/chromium@153.0.0
node Exhibits/Workbench/Construct/BrowserProof.mjs
```

Reports and screenshots: `Exhibits/Gallery/Construct/`. C++ reports include source
hashes and the exact compile command. All evidence is CPU/browser, not Vulkan.

## Sliding catalogue update

The catalogue now includes all existing non-water editor entity templates: Sun,
Moon, Stars, Atmosphere, optical effects, global/local clouds, precipitation,
Wind, local fog, terrain, forest/species, materials and camera controls.
Selecting a tile slides horizontally to a dedicated properties page. Back (or
Escape) returns to the catalogue; a second Escape closes it. Reduced-motion
preferences disable the slide animation. Surfaces use the document's neutral
charcoal rather than the earlier blue-grey palette.

Entries labelled **Edit existing** select the existing entity and mount its
interactive inspector inside the menu. They do not create duplicate native
environment components. Those controls retain their pre-existing browser
bindings; no new connection to the Vulkan game process is claimed. **Camera**
creates a native camera record; **Camera controls** edits the existing browser
camera inspector. Native primitive creation still uses the real C++ bridge.

`SlidingProof.mjs` checks the slide workflow, Wind speed editing, Sun/Terrain/
Camera/local fog pages, Back/Escape navigation and mobile width. It uses real
pointer input for Back and timer-based transform checks because the sandbox
headless Chromium can stop RAF polling after complex animated inspector pages.
