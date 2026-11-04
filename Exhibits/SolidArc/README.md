# SolidArc CAD workspace in Frontier

Source-only integration against `streamlinkinbox/Frontier` commit
`e72d319239dffdb961629150b9de6f1e7dc618c6`, branch `arena/88b72faa-frontier`.
SolidArc source/fixes: `SultanAladin/Slate` `c3a187909483aa8182ba96cf4916e3542a09395e`.

## Scope

- Reuse FrontierImGui, FrontierInterface, FrontierEditorUI, fonts, content directory and pinned dependencies.
- Add the CAD B-rep library, document/UI/viewport, CAD SVG symbols and native CAD workspace.
- Attach `.solidarc` documents to the existing project browser; display decoded geometry and open in SolidArc.
- Keep the existing Everest startup and editor behavior. CAD opens in a separate workspace process;
  this does not add SolidArc tools directly to the Everest scene viewport.
- Retain Shift straight drawing, curve multiselection/Delete, safe component deletion, surface tracing,
  live surface-distance links and surface-attached cutters from the current SolidArc source.
- Do not import Slate's unrelated applications, demos or engine framework. Do not add T-splines/SubD.
- Do not import or edit the supplied vehicle project. Validation uses disposable fixtures/copies.

## Build

Use the existing Frontier dependency/font setup. `Automation/UIDependencies.json` adds only the pinned volk loader.
Configure/download remain separate: CMake never fetches packages.

```sh
python Automation/PrepareUI.py
python Automation/PrepareFonts.py
cmake -S . -B build -DFRONTIER_BUILD_SOLIDARC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Frontier SolidArc
```

Use the existing Windows toolchain/presets. On Linux, local verification additionally needs
`-DFRONTIER_HOST_VERIFICATION=ON`. In a headless environment, disable GLFW X11/Wayland to compile the null
windowing substrate; that build is for verification, not a usable desktop distribution.
`-DFRONTIER_BUILD_SOLIDARC=OFF` disables CAD targets and the Windows browser connection.

The current editor preview packager detects CAD-enabled Frontier and requires the companion
`SolidArcWorkspace.exe`. It copies CAD options, a sample and the volk notice, and verifies CAD icons/font.
The existing signature/antivirus policy and the restricted two-application distribution packager are unchanged.
The branch's Windows package (`Binaries/FrontierSceneViewport-Windows-x64.zip`) carries the cross-built
`SolidArcWorkspace.exe`; it has not been started on Windows. The version resource (`Package/SolidArcWorkspace.rc`)
is compiled with MSVC only, as Frontier's own is: the Zig cross-toolchain has no resource compiler.

## Verification

```sh
cmake --build build --target FrontierSolidArcBrowserIntegrationValidation LigerShiftDrawingValidation
ctest --test-dir build -R FrontierSolidArcBrowserIntegrationValidation --output-on-failure
python Exhibits/SolidArc/Code/Integration/WindowsPackageValidation.py
```

Additional native targets: LigerCutterPlacementValidation, LigerSelectionValidation, LigerCageToolsValidation,
LigerCurveEditingValidation, LigerSurfaceTraceValidation and SolidArcDocumentValidation. Tests accepting a vehicle
file must be supplied an external copy/reference; no vehicle is included by the source integration.
Rendered workflows are under `Code/Integration/*Check.txt` and use SolidArcCapture with Vulkan.

Fresh evidence for this port is retained in the source session under
`SolidArc/Integration/Frontier-e72d319/Validation`, with its audit and source manifest alongside it.
It was merged into `arena/88b72faa-frontier` in `4a6b6b7` (SolidArc only). The results of this branch's own
runs are in `Documentation/Tracker.md`.

The 51 ctest walkthroughs (`Code/Scripts/*Walkthrough.txt`) were not part of Slate's evidence. On this branch 40
pass. Twelve scripts were brought up to date without changing their checks: the Drawing options panel covered points
they click, the cube check clicked a face the perspective cube hides, and the Fill walkthrough's picture was missing
(`Images/HtmlReference/EmptyWorkspace.png` is now a capture of the empty workspace). The eleven that still fail,
and why, are listed in the tracker.

Fresh results: 506 native CAD checks, 17 browser CPU checks, 300 CAD Vulkan expectations,
15 browser Vulkan expectations including process hand-off, 869 CAD rows and 9 packaging unit tests.
Windows executable/startup verification remains outstanding.

## Curve workflow revision (2026-11-02)

See [Curve rebuilding guide](CurveRebuildingGuide.md) for the pointer split, editable live surface distance,
Frontier-browser opening and the three dockable primary panels. The in-application **Curve guide** and
**Reset panels** actions are always available in the status bar.

The native curve switches and image opacity/crop sliders call the existing `VectorAuthoringControlSurface`.
`FrontierAuthoringControls` compiles that implementation and Texture Workspace's `PanelChrome` once;
both workspaces link it. No duplicate switch/slider, tab painter or docking implementation is introduced.
`PanelDockspace` owns the Outliner/Viewport/Inspector windows; SolidArc supplies content callbacks only.
Separate OS windows and restart persistence are not claimed.

```sh
cmake --build build --config Release --target SolidArcCurveWorkflowValidation
ctest --test-dir build -C Release -R '^SolidArcCurveWorkflowValidation$' --output-on-failure
```

The focused check exercises the actual pointer interaction, exact NURBS geometry, undo/redo,
serialization, refusals, real dock-tab dragging, reopening/reset, target-only distance edits and inline failures.
Rendered scripts are `Code/Scripts/CurvePointerSplitCheck.txt` (two-line crossing fixture) and
`Code/Scripts/CurveLinkedDistanceEditCheck.txt` (locked 2 m × 2 m reference with two native traces).
Use `SolidArcCapture --open <fixture> --script <script> --exhibits <output>` with a supported Vulkan driver.
Existing integration fixture generation remains in `Code/Integration/CageToolsValidation.cpp`.
