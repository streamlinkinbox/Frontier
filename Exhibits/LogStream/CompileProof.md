# Log Stream — SDK compile proof + rendered workspace captures (2026-10-22)

What this proves: the windowed C++ **type-checks against the genuine upstream
Interface contracts**, and — stronger — the panel has been **registered into
the real upstream editor workspace and rendered** (see "Rendered captures"
below; PNG masters in `Capture/`). It is the production draw code end to end.

What this does NOT prove: that an OS window has opened, or that GLFW/Vulkan
presentation runs. Those need the Windows SDK build on the owner's machine.
The headless captures substitute only the window and the GPU rasterizer.

## Inputs (fetched, not vendored)

| Input | Source |
|---|---|
| Upstream headers @ `9ec758d` | SultanAladin/Frontier branch `Frontier` (owner moved the code there from `arena/db2f9953-frontier` @ `c177821`, an ancestor; every Interface contract used here verified byte-identical, only `VulkanPresentation.cpp` changed) |
| Dear ImGui 1.92.7 | `codeload.github.com/ocornut/imgui` tag `v1.92.7` |
| Vulkan headers 1.4.328 | `codeload.github.com/KhronosGroup/Vulkan-Headers` tag `v1.4.328` |
| GLFW 3.4 headers | `codeload.github.com/glfw/glfw` tag `3.4` |
| VMA 3.3.0 headers | `codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator` tag `v3.3.0` |
| toml++ v3.4.0 | previously pinned checkout, exposed as `toml++/toml.hpp` |

## Commands (g++ 12, Linux — compile only, `-c`)

```
g++ -std=c++20 -c -Wall -Wextra -Werror -DFRONTIER_WITH_INTERFACE_SDK \
  -I Frontier/ -I <upstream> -I <shims> -I <imgui> -I <vulkan>/include \
  -I <glfw>/include -I <vma>/include \
  Frontier/Engine/Interface/LogStreamPanelPresentation.cpp

g++ -std=c++20 -c -Wall -Wextra -Werror -DFRONTIER_WITH_INTERFACE_SDK \
  -DGLFW_INCLUDE_NONE [same includes] \
  Frontier/Applications/Editor/FrontierMain.cpp
```

## Result

- `LogStreamPanelPresentation.o` — clean (re-proved after the 2026-10-22
  1:1 restyle, against both the pinned Frontier ImGui and stock 1.92.7).
  Symbols: `Frontier::LogStreamPanel::ConstructInterface()`,
  `Frontier::MakeLogStreamPanelSpecification(...)`. The presentation now
  draws the approved prototype's exact CSS geometry with DM Sans faces from
  the upstream `TypefaceRegistry`; dropdown menus remain
  `DropdownControlSurface` (see `PARITY.md`).
- `FrontierMain.o` — clean (re-proved 2026-10-22 after the rehost below).
  Symbol: `main` (browser flow; per-project window composed like
  TextureWorkspace — `ControlCentrePanel` notch over a host-owned
  `PanelDockspace`, Viewport | Log Stream at 50/50).
- Upstream is a **C++20** codebase (`std::span` in `MonitorPlacement.h`);
  the portable engine remains C++17-clean for the `Build.bat` floor.
- Bugs this proof caught and fixed in our code:
  1. `ContentDirectory::EncodeContentPath` — C++20 `u8string()` returns
     `std::u8string`; spelled dual-standard now, re-proved at both floors
     (C++17 suite 68/68 after the change).
  2. `RunBrowserListing` — portable-only helper was compiled (unused) in the
     SDK variant; guarded with `#if !defined(FRONTIER_WITH_INTERFACE_SDK)`.


## Rendered captures (Exhibits/LogStream/PanelCapture.cpp)

`PanelCapture.cpp` has two modes. **Panel mode** (`PanelCapture <font.ttf> <dir>`,
run from a directory containing `EngineContent/FontArchives`) hosts
`LogStreamPanel::ConstructInterface()` in a borderless ImGui window at the
prototype's own 1240×830 and CPU-rasterizes `ImDrawData` — legible close-ups:
main, expanded row, channels menu, live search (`<mark>` spans + match
count), overflow menu, new-entries pill. It installs a `TypefaceRegistry`
loaded from those archives so DM Sans (fetched from
`googlefonts/dm-fonts`, staged beside Inter as `FontArchives/DMSans/`) is the
rendered face, exactly as the upstream host supplies it. **Workspace mode**
(`--workspace`, needs `FRONTIER_WITH_WORKSPACE_CAPTURE`) mirrors the
TextureWorkspace recipe (`WindowsTextureStudio.cpp` + `MaterialLibraryPanel`)
exactly as `FrontierMain::RunProjectWindow` now composes it:
`ControlCentrePanel` (real Inter fonts from Frontier- @ `3382938`) over a
host-owned `PanelDockspace` — no project-summary header, 79 px top band for
the trapezoidal notch tab, Viewport (Scene Canvas placeholder) and Log Stream
registered side by side at 50/50 via `ConfigureColumns`; per frame
`PrepareInterface → Integrate → Panels.ConstructInterface →
Centre.ConstructInterface`. Mouse clicks are synthesized to open both dropdowns
(the sweep walks left→right, identifying each popup by position — popup
window names are ID hashes and `IsPopupOpen` hashes against a different
seed outside the panel window); the notch opens via the top-bar button's
own action (`ActivateSettingsPage(QuickControls)`).

Link set beyond the table above: upstream `Engine/Interface` + `UI/Editor` +
`Engine/ContentInterchange` + `Engine/Render` TUs @ `c177821`; **Frontier
ImGui** = pinned `ocornut/imgui@3bae66c` (sha256 in upstream
`Automation/UIDependencies.json`, verified) with upstream's own
`Automation/Patches` A–G applied in order (trapezoidal tabs, tab add button…) —
stock ImGui cannot compile `PanelDockspace.cpp`; **thorvg** @ `6715f99` built
per upstream `Automation/ThorVGSoftware.cmake` (incl. `StageThorVG.py`);
**stb** @ `2c980bb`; **tomlplusplus** @ `1e8829b`. Rasterizer notes: commands
bound to non-atlas GPU textures render as flat vertex colour; the font atlas
is rebuilt before `NewFrame` when the theme pass reloads faces.

Additional bugs this proof caught and fixed:

3. **Header collision (would have broken the Windows build):** my portable
   `Engine/Interface/ContentDirectory.h` shadowed upstream's header of the
   Same name (Same `EncodeContentPath`, plus upstream's browser model class).
   Renamed to `Engine/Interface/ProjectContent.h/.cpp` with non-colliding
   names (`EncodeUtf8Path`, `ResolveProjectContentFile`, …); C++17 floor
   re-proved 68/68 after the rename.
4. **Layout:** Verbosity combo + search field had no item widths (search fell
   off wide rows) — fixed and re-rendered. Superseded by the 1:1 restyle's
   absolute pill layout.
6. **Restyle iteration:** the sparkline's under-fill first shipped as a
   convex-fan fill (a green wedge in close-ups) — replaced with per-segment
   quads; search `<mark>` spans were first computed against the unclipped
   message while rows draw the ellipsised string — recomputed on the drawn
   string; the overflow menu now anchors under its button via a
   right-aligned next-window pivot. All re-rendered and verified.
5. **Host composition (changed 2026-10-22, owner direction):** the project
   window must NOT carry the `EditorWorkspace` project-summary header
   (`Back · ProjectZero · 1 levels · 0 catalogue entries`). `FrontierMain`
   was rehosted from its hand-assembled host (own `PanelDockspace` +
   text-only `NotchMotion`) onto the TextureWorkspace composition:
   `ControlCentrePanel` (trapezoidal expansion-notch tab, appearance, fonts;
   preferences under the project's `Saved/`) + host-owned `PanelDockspace`
   (key `Frontier project panels`, insets 79/0, tab extent 140x26) with the
   honest Scene Canvas Viewport placeholder and the Log Stream at 50/50.
   Compile-proved clean; the captures above use this composition. The
   earlier plan to rehost onto `EditorWorkspace` is superseded.

## Workspace capture rebuild recipe (Linux proof environment)

So the captures can be regenerated from scratch (all inputs fetched, none
vendored): clone SultanAladin/Frontier branch `Frontier` @ `9ec758d`;
codeload `ocornut/imgui@3bae66c` + apply upstream `Automation/Patches`
A–G with `patch -p1` in order (cumulative); `ocornut/imgui v1.92.7`,
`KhronosGroup/Vulkan-Headers v1.4.328`, `glfw/glfw 3.4`,
`GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator v3.3.0`,
`marzer/tomlplusplus@1e8829b`, `nothings/stb@2c980bb`, `thorvg/thorvg@6715f99`,
`googlefonts/dm-fonts` (DM Sans static OTFs staged as
`FontArchives/DMSans/` beside the Inter set from SultanAladin/Frontier-
@ `3382938`). thorvg per `Automation/ThorVGSoftware.cmake` without cmake:
run `StageThorVG.py`, write the 5-define `config.h`, compile
`src/common`, `src/renderer` (minus `tvgInitializer.cpp`),
`src/renderer/sw_engine`, `src/loaders/svg`, `src/loaders/raw` + the staged
initializer at C++14 with `TVG_STATIC TVG_BUILD NOMINMAX`. Workspace binary:
g++ 12, `-std=c++20 -DFRONTIER_WITH_INTERFACE_SDK
-DFRONTIER_WITH_WORKSPACE_CAPTURE -DGLFW_INCLUDE_NONE -DTVG_STATIC`, includes
= this repo's `Frontier/`, upstream root, patched imgui, toml++, Vulkan/GLFW/
VMA headers, thorvg `inc` + staging + the five source dirs, stb. TUs: our
`Engine/Diagnostics/*.cpp`, `LogStreamPanel{,Presentation}.cpp`,
`PanelCapture.cpp`; upstream `Engine/Interface` (AppearanceCodec,
AppearanceInspector, ConfigurationInspector, ConfigurationStructure,
ConfirmationDialogue, ContentDirectory, ControlKit, FontCodec, GlyphSpace,
IconArt, IconPresentation, InputPreferencesCodec, MonitorPlacement,
MotionIntegrator, NotchMotion, NotificationSequence, PanelDockspace,
PanelDockspaceLayout, PanelDockspaceMonitor, PanelExpansionTab, PixelSpace,
TextEntryState, TextFileReplacement, ThemeStructure, TypefaceRegistry,
VectorAuthoringControls, VectorCodec), `UI/Editor` (ControlCentrePanel,
FoundationPanel, ContentBrowserPanel),
`Engine/ContentInterchange/ProjectSpaceCodec.cpp`, `Engine/Render`
(StbImageImplementation, StbImageWriteImplementation), imgui
(imgui, imgui_draw, imgui_tables, imgui_widgets); link + thorvg objects +
`-pthread`. Run from a directory containing `EngineContent/FontArchives`.
This environment was rebuilt from zero in minutes after a sandbox reset using
exactly this recipe (2026-10-22).

## Addendum (2026-10-22): StackRail kit + native Outliner

- `Frontier/Engine/Interface/StackRailControls.cpp` and
  `SceneDirectoryPanelPresentation.cpp` compile clean with the §Commands line
  (`-std=c++20 -Werror`, upstream headers now in-repo at the repository root);
  `nm` confirms `SceneDirectoryPanel::ConstructInterface` and
  `MakeSceneDirectoryPanelSpecification` exports.
- `FrontierMain.cpp` re-proved clean (SDK and headless) after wiring the
  Outliner (`ConfigureStackedPanels("Outliner", "LogStream", 420)` in the
  right column; columns remain 50/50; spawn requests serviced with real
  `SceneRecord`s).
- The workspace capture binary was rebuilt at 49 TUs (the recipe above plus
  `Engine/Scene/{RecordStore,RecordEntry,SceneDirectory}.cpp` and
  `Engine/Interface/{SceneDirectoryPanel,SceneDirectoryPanelPresentation,StackRailControls}.cpp`),
  run from a rebuilt environment (imgui `3bae66c` + patches A–G from
  `Automation/Patches/`, thorvg staged by `Automation/StageThorVG.py`).
  New masters in `Capture/`: `workspace-outliner.png`,
  `workspace-outliner-spawn.png`, `workspace-studio-outliner.png` (the earlier
  `workspace-studio.png` master is preserved — the owner-approved composition
  without the Outliner).
