# Project Everest — native editor UI exhibit

## Scope

This exhibit is a native C++/Dear ImGui editor UI, not a browser application. The HTML under `Frontier/Projects/Project-VehicleHmiDemo/Reference/` remains reference material; it is not served or delivered as the UI.

The native launch flow is:

1. Open the existing `.frontier` Project Browser and choose **Project Everest**.
2. Enter the native workspace: VehicleHMI Outliner on the left, Viewport above Log Stream in the centre, and Inspector on the right.

The project chooser reuses the existing `RunBrowserWindow` flow from SultanAladin's `arena/11f31835-frontier` main/UI branch. Its original `.frontier` enumeration and project-list controls are factored into `ConstructProjectBrowserInterface` so the native evidence harness exercises the exact same controls. No application-selection page or second browser was introduced. This chooser is distinct from `UI/Editor/ProjectBrowserPanel`, which browses `.projectspace` content; that catalogue remains separate.

The Outliner follows VehicleHMI's group order, row behavior, metrics, search, selection, visibility toggles, state readouts, and styling. Lucide 1.57.0 path geometry is drawn natively; the custom environment SVGs are rasterized through the existing ThorVG/ImGui texture presentation, with checked vector compatibility variants for SVG filter primitives unsupported by the pinned renderer. The native icons are exercised by the workspace capture test.

The native Log Stream, Outliner, Inspector, and reusable Sun, Atmosphere, Clouds, Fog, and Wind records are integrated into Project Everest. The project intentionally declares no opening scene or code image, so the Viewport remains an honest empty canvas and the log says no renderer is connected.

The VehicleHMI/ReSTIR renderer, project scene, and hardware paths are **not** part of this UI slice. The exhibit makes no RTX/RT-core or GTX SDF/BVH fallback claim. No product binary is included in Git.

## Native evidence

The PNGs below are generated from C++ ImGui draw data by `Exhibits/LogStream/PanelCapture.cpp`; they are not HTML renders or edited upstream references. Browser capture drives the existing native `.frontier` project buttons and verifies selection of the Project Everest descriptor. The workspace harness exercises the production Control Centre, PanelDockspace, Outliner, Inspector, and Log Stream in a deterministic ImGui frame, and checks that the custom Outliner icon set is ready. It substitutes a CPU rasterizer for GLFW/OS-window and GPU presentation.

| Image | Native state shown |
|---|---|
| [`Images/NativeProjectSelection.png`](Images/NativeProjectSelection.png) | Existing `.frontier` project list; the capture test clicks Project Everest and validates its descriptor path. |
| [`Images/NativeWorkspace.png`](Images/NativeWorkspace.png) | Project Everest workspace with the VehicleHMI Outliner, selected Sun Inspector, idle Viewport, and Log Stream. |
| [`Images/NativeWorkspaceReference.png`](Images/NativeWorkspaceReference.png) | Inherited upstream workspace reference, retained for comparison; not this project's capture. |
| [`Images/NativeControlCentreReference.png`](Images/NativeControlCentreReference.png) | Inherited upstream Control Centre reference, retained for comparison; not this project's capture. |

The native entry point is compiled in the `FrontierNativeEntry` object target with the GLFW/interface SDK headers. The exhibit does **not** launch a GLFW OS window or Vulkan presentation; renderer integration, RTX/GTX hardware execution, and performance remain unverified.

## Native project/record validation

`Code/NativeProjectValidation.cpp` exercises the real TOML `.frontier` decoder and browser enumeration, verifies the empty opening-scene/code-image fields and Content directory, then constructs reusable environment records and checks their registration in the Outliner and creation/removal of Inspector descriptions. It also verifies that a live Inspector scalar field edits the owning Sun record. This is record-lifecycle validation, not renderer validation.

## Reproduce

Prerequisites: CMake 3.20+, a C++20 compiler, Python 3.11+, and a build tool supported by CMake. Prepare the pinned UI dependencies and fonts from the repository root:

```sh
python3 Automation/PrepareUI.py
python3 -m pip install fonttools
python3 Automation/PrepareFonts.py
```

Configure, build, and run the native checks:

```sh
cmake -S Exhibits/ProjectEverest/Code -B Build/ProjectEverest
cmake --build Build/ProjectEverest --target Frontier FrontierNativeEntry NativeProjectValidation NativeUIEvidence
ctest --test-dir Build/ProjectEverest --output-on-failure
```

`NativeUIEvidence` writes `browser-projects.png` and `native-workspace.png` under its output directory. Browser mode exercises the existing `.frontier` list and clicks the Project Everest descriptor. CTest runs both browser and workspace capture. `FrontierNativeEntry` is compile-only, while `Frontier` is the portable headless host used by the smoke test. All binaries and intermediate outputs remain under ignored `Build/`.

## Current local result

On 2026-10-31 with GCC 12.2, CMake 4.4.4, and Ninja 1.13.2, all four CTests passed. The native project/record validator passed 13 checks with zero failures; the existing browser flow selected `ProjectEverest.frontier`; and the workspace evidence reported all 10 custom VehicleHMI Outliner icons ready. See [`Results/NativeValidation.txt`](Results/NativeValidation.txt) for the exact commands and limits.
