# Shared Panel Docking Exhibit

Shared panel hosting and personal workspace layout persistence; not a completed editor workspace.

## Implementation and ownership

| Layer                           | Responsibility                                                     |
|---------------------------------|--------------------------------------------------------------------|
| Engine/Interface/PanelDockspace  | Stable registrations; patched native DockSpace; open/close/placement. |
| UI/Editor/EditorWorkspace        | Connects original editor-owned panels without duplicating content. |
| UI/Editor/ControlCentrePanel     | Workspace preference, real confirmation, personal projectspace save. |
| Exhibits/UIFoundation/WindowsUI  | Existing GLFW/DX11 display adapters and secondary-window lifecycle. |

- The inherited ImGui patches A–F, correction G, and native docking handle dragging, reordering and splitting.
- Geometry comes from the pinned `Engine/Editor/EditorHost.cpp`; colours follow the applied existing theme.
- Content Browser defaults to its draggable trapezoidal drawer. Settings → Workspace offers Draggable / Dockable.
- In Dockable mode, the browser opens as a tab; Notification Stream remains available through the native `+`.
- The browser entry in `+` is disabled in Draggable mode, preventing a competing second presentation.
- The same ContentDirectory, filters, selection and grid/list choice survive hosting changes and close/reopen.
- Hidden browser content relinquishes search/pointer focus. Hosted browser keyboard navigation remains available.
- Closing the dockable browser leaves it closed until `+` reopens it. Choosing Draggable restores the drawer.
- Changing presentation preserves the original directory, filters, selection and grid/list choice.
- Opening the stream as a tab suppresses its Quick Controls copy, not its underlying notification history.
- The Control Centre remains a drawer. No imaginary scene/tools panels or competing docking algorithm were added.

## Optional OS windows

**Control Centre → Settings → Workspace → Allow detached OS windows** is disabled by default.
Apply opens the real Apply/Cancel confirmation. Cancel preserves the draft and existing file.

The personal `.projectspace` embeds `[workspace.hosting]`, `format = 1`,
`allow_detached_windows = false`, `dockable_content_browser = false`. Both choices are staged and confirmed.
The browser field is optional when reading older personal files, defaulting to Draggable.
No new TOML/INI sidecar is written.
Project settings cannot enable machine-local detached windows; unrelated personal overrides remain unchanged.
Legacy hosts without a personal projectspace apply the preference for the session only.

When enabled and supported by both display/platform adapters, ImGui platform windows use the existing GLFW and
DX11 callbacks. The panel context menu also offers explicit detach/dock-back. Explicit detach requests a normal,
OS-decorated window on the current monitor, which can then be moved to another monitor.
Disabling requests return of registered panels to the main dockspace without reconstructing content.
Runtime disable destroys secondary windows only; the main GLFW viewport allocation survives re-enabling.
The Windows exhibit updates/renders secondary windows while the main window is minimized and destroys secondary
resources before backend shutdown. GLFW still owns native window/input lifetime. DX11 remains exhibit-only;
the default editor now uses VulkanPresentation and the pinned ImGui Vulkan platform-window adapter; no scene renderer
has been imported. DX11 remains a separate comparison executable.

## Verification and evidence

```text
cmake --build _ClaudeScratch/build/ui --target DockingValidation
ctest --test-dir _ClaudeScratch/build/ui -R DockingValidation --output-on-failure
python Exhibits/ControlCentre/Code/ValidateNavigation.py
```

The full driver runs all 14 tests in Debug and with address/undefined-behaviour sanitizers, cross-builds and inspects
the Windows package, and regenerates these seven native CPU captures alongside the previous exhibits:

| Capture                     | Demonstrates                                           |
|-----------------------------|--------------------------------------------------------|
| Images/DraggableDefault.png | Default draggable browser tab, not a docked browser.    |
| Images/DockedContent.png    | Original Content Browser in the native patched tab.    |
| Images/DockedStream.png     | Native tab selection showing the existing stream.      |
| Images/SplitPanels.png      | Native builder split, preserving real browser content. |
| Images/WorkspaceSettings.png| Workspace preferences, saved-layout status and reset. |
| Images/RestoredTabs.png     | Tab order/selection rebuilt in a fresh ImGui context.  |
| Images/RestoredSplit.png    | Native split proportions rebuilt after restart.        |

`Code/DockingValidation.cpp` exercises actual pointer input for the patched `+`, catalogue selection, native tab
selection, native tab close and horizontal tab reordering. It also verifies native builder splitting/dock-back,
real hosted browser pointer/keyboard input, hidden-search suspension, close-all/reopen, stable content identity,
default draggable mode, narrow/scaled bounds, modal Cancel/Apply, personal reload, project isolation, malformed hosting
refusal, and unsupported-adapter refusal. It does not simulate OS windows or claim a native drag/split gesture test.
Retained results and source/image hashes: `../ControlCentre/Results/NavigationChecks.json`.

## Frontier-owned layout archive

Personal `.projectspace` embeds `[workspace.layout]`, format 1. Frontier captures stable panel keys rather than
native IDs or raw INI; existing native DockBuilder reconstructs docking. `ImGui::GetIO().IniFilename` stays null.
The archive preserves open/closed panels, ordered tabs and selection, split trees/ratios, floating groups and window
rectangles relative to the main viewport, plus the draggable browser drawer's open state. Content is not duplicated.
Detached-window permission remains default-off and cannot be enabled by a layout. Unsupported/disabled native windows
return to the main dockspace. Default browser presentation remains Draggable.

Autosave waits eight initial frames, two seconds of unchanged geometry and no pointer drag/modal; failures retry after
ten seconds. Destruction flushes before platform-window teardown; a minimized main window retains its last valid snapshot.
Saving merges the latest personal container and changes only workspace.layout, preserving other sections, override flags
and unapplied drafts. Missing personal containers use engine defaults without activating unrelated personal overrides.
Legacy/non-projectspace hosts are session-only. No standalone TOML or INI file is created.

Validation limits: 16 panels, 63 regions, 17 roots, depth 16, 64 KiB serialized layout; bounded finite geometry, stable
unique keys/tabs, reachable acyclic unshared regions, one main root, and selected-tab membership are required.
Missing layout uses defaults. Invalid/unsupported optional layout falls back and stays write-protected until
**Settings → Workspace → Reset saved layout**. A corrupt whole container is never overwritten, even by Reset.

`Code/WorkspaceLayoutValidation.cpp` starts fresh contexts to test tabs/order/selection, splits, floating groups,
closed/all-closed panels, drawer visibility, debounce/unchanged timestamps, latest-document merging, preserved drafts,
layout-only containers, malformed/cyclic/versioned layouts, actual pointer reset, and atomic-write failure/retry.
A synthetic archive tests default-off policy reattachment; it does not execute native OS windows.

## Remaining phases and limitations

- Layout persistence is implemented; migration of future schema versions is not. Unsupported versions are protected.
- Monitor work-area/density reconciliation and live topology recovery are implemented and tested synthetically.
  Physical monitor removal, mixed-DPI changes and OS-level focus/minimize/close still require Windows execution.
- Real Windows OS-window drag-out/dock-back, secondary swapchains, focus, minimize/close, mixed-DPI/multi-monitor
  transitions, unplugging displays and toggle-off/on require Windows runtime testing; compilation is not execution.
- No Windows runtime, GPU or MSVC execution is available in this editing environment. No macOS setup or Actions.
- The Vulkan UI host now wires platform-window display integration; physical GPU/OS execution remains unverified.
  This is UI-only presentation, not a scene renderer. No Linux product binary is delivered.

## Adjacent plus and opaque tab correction

`Automation/Patches/PatchG-AdjacentTabAddButton.patch` fixes the inherited add-button issue without replacing docking.
The native trailing section already supplies spacing; the old metric incorrectly compensated for an overlap that
only exists inside the central section, producing the excessive gap. Correction G removes that extra advance,
sizes the disc/hit rectangle from the real strip, and centres geometric plus strokes on the same centre.
Native pointer `+` opening still works after reordering, splitting, closing and reopening tabs.

`ThemeStructure::QueryDarkestSurface` selects the lowest-luminance named background surface (not text or border),
then forces alpha to one. The shared top/bottom expansion tabs and native trapezoidal tab states use that token.
Pixel checks cover both drawers across all eight themes at 100% and 125%; native tab fill tokens remain opaque.
Plus pixel bounds, strip centring and tab adjacency are checked at both scales, with one and multiple tabs.
All seven patches were reverted/reapplied/verified; `Results/PatchLifecycle.txt` retains the output.

## Vulkan and monitor recovery continuation

`FrontierUI.exe` is the Vulkan editor UI host. `FrontierUIDX11.exe` retains the isolated comparison exhibit.
`Engine/Interface/VulkanPresentation` owns a Vulkan 1.1 instance/device, a graphics/presentation-capable queue and the main
UI swapchain; pinned `imgui_impl_vulkan` owns secondary-window swapchains and callbacks. GLFW owns OS windows/input.
Vulkan entry points load through GLFW; headers are checksummed in UIDependencies.json. There is no Vulkan SDK, loader
binary, authored shader or scene-renderer import. FIFO presentation, resize/out-of-date/suboptimal recovery, main-window
minimization with active secondary windows and device-idle-before-secondary-teardown are wired.

Optional layout rectangle `monitor` tables contain work-area x/y/width/height, dpi and absolute window_x/window_y.
Old format-1 layouts without this metadata remain readable. Restored geometry preserves surviving-monitor overlap,
reconciles saved/current density and fits all edges to a current work area, including negative monitor coordinates.
Live monitor/work-area changes invoke existing platform position/size callbacks before NewFrame. ImGui's native DPI
scaling remains responsible for live transitions; Frontier does not apply a second multiplier. Minimized secondary
windows defer recovery until restored. Empty/transient monitor enumeration does not forcibly relocate windows.

`MonitorPlacementValidation` exercises removed monitors, negative coordinates, density changes, small work areas,
nonfinite input, idempotence, synthetic platform callback invocation and deferred minimized-window recovery.
`WorkspaceLayoutValidation` additionally refuses malformed/zero-density monitor metadata. These are host-side checks,
not physical monitor/GPU execution. The native error-dialog fallback is only the start of general severe-error work;
the supplied full-screen error UI, presets, notification actions and workspace shortcuts remain pending.
