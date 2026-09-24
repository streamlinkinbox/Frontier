# Native Construct integration

`NativeConstructPanel.h` is part of the actual C++ `EditorHost` via
`Tools/Build/Patches/NativeConstruct.patch` (applied after
`MainEditorIntegration.patch`). It is not the browser/native-authoring-worker
bridge. The native host's **+** and **Shift+A** open the panel.

## Supported engine entities only

The catalogue projects the live project roster, excludes folders and invalid
records, and never imports the HTML template list. The tested Project-Zero
fixture exposes its actual camera/post-process records and `CelestialSequence`:
Atmosphere, Sun, Sky, Stars, Moons, Lens Flare, Wind, Cloud Layer, Precipitation,
Local Cloud, Height Fog, Atmospheric Fog, Local Volumetric Fog and Rainbow.
Loaded scene geometry and lighting appear when they actually exist in the
roster. No terrain, forest or water generators are offered.

These environment systems already exist as singleton world records. The menu
**selects, enables and edits those records**; it does not manufacture duplicate
Sun/Wind systems or pretend unsupported generators exist. "Enable in world"
unhides the record and its ancestor visibility gates. The inspector exchange
commits edits directly to the project settings used by the engine. Additional
mesh/camera spawning via `ConstructWorld` is still the separate CPU authoring
bridge; this native panel does not add that unfinished GPU scene-rebuild path.

## Interaction and inspector reuse

- Dark charcoal surfaces, category navigation, name search and icon tiles.
- Selecting a tile selects the actual outliner row and slides into properties.
- Properties reuse `InspectorPanel::Record(..., Embedded=true)` and the real
  `EditorInspectorSequence` exchange. There is only one active property editor,
  not a duplicate sheet or browser-rendered approximation.
- Back/Escape returns to entities; Escape again closes the panel.
- The properties page resolves `InspectorKey` every frame; row moves cannot
  redirect edits or leave selection attached to a different row.
- No viewport was added to the inspector-only workspace.

## Folder/icon parity

Scene and World folders now explicitly use the same compact category SVGs as
`outliner-icons.jsx`. Environment retains the exact HTML environment folder.
Generic groups use the HTML generic folder; no water folder is invented.
Compact transforms remove gallery text/title metadata exactly as HTML does.
Folder names retain their case rather than being forced into spaced uppercase;
extra folder status badges have been removed. Icons are vertically centred in
30 px cells, with 39 px rows, 13 px hierarchy steps and consistent label spacing.

Cloud/fog artwork no longer appears as unsupported-filter placeholders.
`BakeNativeIcons.mjs` rasterizes approved SVGs with their filters intact; the
native loader verifies **exact source bytes** before using a bake. A stale bake
is rejected. The source SVGs and verified bake manifest ship together. Semantic
fog and post-process artwork is shared between the palette and outliner, rather
than using generic mesh icons.

## Reproduce

```sh
# Native sources/dependencies reconstructed at the pinned engine revision.
python3 Exhibits/Workbench/Construct/RunNativePanel.py
python3 Exhibits/Workbench/Construct/RunNativePanel.py --reuse-target --sanitize
```

Requires the configured `gh` connection, g++/C++20 and the normal native proof
build dependencies. The runner uses a separate staging tree and never changes
branches or resets user source. It copies shipped icons into the native runtime
root and uses the pinned native font assets, avoiding fallback-font captures.

Evidence: `Exhibits/Gallery/NativeConstruct/`.
111 native checks cover actual tile input, search, the embedded Wind slider,
write-back to engine Wind settings, enabling the real system through ancestor
gates, selection/identity after row reordering, Back/Escape, supported roster
membership and folder artwork. `GameSyntax.json` records a successful project
translation-unit check. These are native ImGui/CPU executions and native draw-list
captures; the Windows application and hardware Vulkan execution are unverified.
