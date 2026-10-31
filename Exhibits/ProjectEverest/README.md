# Project Everest — native editor UI exhibit

## Scope

This exhibit is a native C++/Dear ImGui port of the editor shell, not a browser application. The HTML in `Frontier/Projects/Project-VehicleHmiDemo/Reference/` is retained as reference material for the original VehicleHMI/ReSTIR shell; it is not the deliverable and it is not served by this exhibit.

The native flow is:

1. Select **Frontier Editor** in the native application browser.
2. Select **Project Everest** from the repository's `.frontier` descriptors.
3. Open the native workspace: Outliner on the left, Viewport above Log Stream in the centre, and Inspector on the right.

`Frontier/Applications/Editor/FrontierMain.cpp` owns the application-to-project-to-workspace flow. It reuses the SultanAladin main/UI branch's `ControlCentrePanel`, ControlKit/theme, and `PanelDockspace`; the native Log Stream, Outliner, Inspector, and environment records are ported from the egoist branch where appropriate. The Outliner and Inspector show reusable Sun, Atmosphere, Clouds, Fog, and Wind records. Project Everest intentionally declares no opening scene or code image, so the Viewport is an honest empty canvas and the log explicitly says that no renderer is connected.

The VehicleHMI/ReSTIR renderer, project scene, and hardware paths are **not** part of this UI slice. The exhibit makes no RTX/RT-core or GTX SDF/BVH fallback claim. No product binary is included in Git.

## Native evidence

The PNGs below are generated from the actual C++ ImGui draw data by `Exhibits/LogStream/PanelCapture.cpp`; they are not HTML renders or edited upstream references. Browser capture drives the real native ImGui buttons and verifies that they select the expected Project Everest descriptor. The workspace harness calls the production Control Centre, PanelDockspace, Outliner, Inspector, and Log Stream units in a deterministic ImGui frame; it does not call `RunProjectWindow`, and substitutes a CPU rasterizer for GLFW/OS-window and GPU presentation.

| Image | Native state shown |
|---|---|
| [`Images/NativeApplicationSelection.png`](Images/NativeApplicationSelection.png) | Native Frontier application-selection page. |
| [`Images/NativeProjectSelection.png`](Images/NativeProjectSelection.png) | Native `.frontier` project list with Project Everest first and ready to open. |
| [`Images/NativeWorkspace.png`](Images/NativeWorkspace.png) | Project Everest workspace with Outliner, selected Sun Inspector, idle Viewport, and Log Stream. |
| [`Images/NativeWorkspaceReference.png`](Images/NativeWorkspaceReference.png) | Inherited upstream workspace reference, retained for comparison; not this project's capture. |
| [`Images/NativeControlCentreReference.png`](Images/NativeControlCentreReference.png) | Inherited upstream Control Centre reference, retained for comparison; not this project's capture. |

The native entry point is also compiled in the `FrontierNativeEntry` object target with the GLFW/interface SDK headers. The exhibit does **not** launch a GLFW OS window or Vulkan presentation; those, renderer integration, RTX/GTX hardware execution, and performance remain unverified.

## Native project/record validation

`Code/NativeProjectValidation.cpp` exercises the real toml++ `.frontier` decoder and browser enumeration, verifies the empty opening-scene/code-image fields and Content directory, then constructs reusable environment records and checks their registration in the Outliner and creation/removal of Inspector descriptions. It also verifies that a live Inspector scalar field edits the owning Sun record. This is record-lifecycle validation, not renderer validation.

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

`NativeUIEvidence` writes `browser-applications.png`, `browser-projects.png`, and `native-workspace.png` under its output directory. The browser mode performs the click-driven application/project selection test; CTest runs both browser and workspace capture. `FrontierNativeEntry` is compile-only, while `Frontier` is the portable headless host used by the smoke test. All binaries and intermediate outputs remain under ignored `Build/`.

## Current local result

On 2026-10-30 with GCC 12.2 and CMake 4.4.4: all four CTest tests passed; the native project/record validator passed 13 checks with zero failures; the native browser flow selected `ProjectEverest.frontier`; both browser pages and the editor workspace were captured. See [`Results/NativeValidation.txt`](Results/NativeValidation.txt) for the exact commands and limits.
