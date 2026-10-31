# Project Everest — native editor UI exhibit

## Scope

This exhibit is a native C++/Dear ImGui editor UI, not a browser application. The HTML under `Frontier/Projects/Project-VehicleHmiDemo/Reference/` remains reference material; it is not served or delivered as the UI.

The native launch flow is:

1. Open the existing `.frontier` Project Browser and choose **Project Everest**.
2. Enter the native workspace: VehicleHMI Outliner on the left, Viewport above Log Stream in the centre, and Inspector on the right.

The production `.frontier` chooser remains the existing `RunBrowserWindow` from SultanAladin's `arena/11f31835-frontier` main/UI branch; its original ImGui list is inline and unchanged. No alternate chooser, extracted presentation, or application-selection page is added. This flow is distinct from `UI/Editor/ProjectBrowserPanel`, which browses `.projectspace` content; that catalogue remains separate.

The Outliner follows VehicleHMI's group order, row behavior, metrics, search, selection, visibility toggles, state readouts, and styling. Lucide 1.57.0 path geometry is drawn natively; the custom environment SVGs are rasterized through the existing ThorVG/ImGui texture presentation, with checked vector compatibility variants for SVG filter primitives unsupported by the pinned renderer. The native icons are exercised by the workspace capture test.

The native Log Stream, Outliner, Inspector, and reusable Sun, Atmosphere, Clouds, Fog, and Wind records are integrated into Project Everest. Sun uses its four-section VehicleHMI Inspector; Atmosphere, Clouds, Fog, and Wind expose grouped native controls bound to their owning records. The project intentionally declares no opening scene or code image, so the Viewport remains an honest empty canvas and the log says no renderer is connected.

The VehicleHMI/ReSTIR renderer, project scene, and hardware paths are **not** part of this UI slice. The exhibit makes no RTX/RT-core or GTX SDF/BVH fallback claim. No product binary is included in Git.

## Native evidence

The retained PNG is generated from C++ ImGui draw data by `Exhibits/LogStream/PanelCapture.cpp`; it is not an HTML render or edited upstream reference. The workspace harness exercises the production Control Centre, PanelDockspace, Outliner, Inspector, and Log Stream in deterministic ImGui frames, and checks that the custom Outliner icon set is ready. Sun's Inspector is the native four-section VehicleHMI dashboard (Overview, Daylight, Light & colour, Disc & optics); its scalar sliders delegate to the shared main/UI ControlKit through PixelSpace. The harness substitutes a CPU rasterizer for GLFW/OS-window and GPU presentation. The production browser remains the existing `RunBrowserWindow`; it is compile-checked, while the native project validator verifies `.frontier` descriptor enumeration.

| Image | Native state shown |
|---|---|
| [`Images/NativeWorkspace.png`](Images/NativeWorkspace.png) | Project Everest workspace with the VehicleHMI Outliner, selected Sun Inspector, idle Viewport, and Log Stream. |
| [`Images/NativeWorkspaceReference.png`](Images/NativeWorkspaceReference.png) | Inherited upstream workspace reference, retained for comparison; not this project's capture. |
| [`Images/NativeControlCentreReference.png`](Images/NativeControlCentreReference.png) | Inherited upstream Control Centre reference, retained for comparison; not this project's capture. |

The native entry point is compiled in the `FrontierNativeEntry` object target with the GLFW/interface SDK headers. The exhibit does **not** launch a GLFW OS window or Vulkan presentation; renderer integration, RTX/GTX hardware execution, and performance remain unverified.

## Native project/record validation

`Code/NativeProjectValidation.cpp` exercises the real TOML `.frontier` decoder and browser enumeration, verifies the empty opening-scene/code-image fields and Content directory, then constructs reusable environment records and checks their registration in the Outliner and creation/removal of Inspector descriptions. It verifies the Sun's manual east-origin azimuth and automatic celestial-solver path, plus live grouped field bindings and write-through edits for Sun, Atmosphere, Clouds, Fog, and Wind. This is record/Inspector validation, not renderer validation.

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

`NativeUIEvidence` writes `native-workspace.png` under its output directory by rendering the native ImGui workspace draw data. CTest runs the workspace capture together with descriptor/record validation and the portable headless smoke test. `FrontierNativeEntry` is compile-only, while `Frontier` is the portable headless host used by the smoke test. All binaries and intermediate outputs remain under ignored `Build/`.

## Current local result

On 2026-10-31 with GCC 12.2, CMake 4.4.4, and Ninja 1.13.2, all three CTests passed. The native project/record validator passed 23 checks with zero failures, and the workspace evidence reported all 10 custom VehicleHMI Outliner icons ready. The existing `.frontier` chooser is compile-checked through `FrontierNativeEntry`; its descriptor enumeration is covered by the project validator. See [`Results/NativeValidation.txt`](Results/NativeValidation.txt) for commands and limits.
