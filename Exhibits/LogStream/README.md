# Log Stream native validation

The C++ diagnostics engine, Log Stream panel, project descriptor decoder, and browser enumeration are reused by the Project Everest native UI slice. The VehicleHMI/ReSTIR HTML under `Frontier/Projects/Project-VehicleHmiDemo/Reference/` is design/reference material only; this exhibit does not serve it.

## Current Project Everest evidence

The native workspace capture uses the production `LogStreamPanelPresentation.cpp`, with the workspace, Outliner, Inspector, and Control Centre. Its ImGui draw data is CPU-rasterized by `PanelCapture.cpp` to `Exhibits/ProjectEverest/Images/NativeWorkspace.png`. The native browser/workspace CTest flow and C++ build are configured in `Exhibits/ProjectEverest/Code/CMakeLists.txt`; reproduce them using `Exhibits/ProjectEverest/README.md`. Exact results and limits are recorded in `Exhibits/ProjectEverest/Results/NativeValidation.txt`.

The workspace fixture shows three representative Project Everest session messages, including the selected Sun Inspector and explicit scene-canvas/renderer-disconnected status. These are native UI pixels, but not an OS-window, Vulkan-renderer, or GPU capture.

## Portable Log Stream validation source

`LogStreamValidation.cpp` contains focused headless checks for ring overwrite/loss accounting, multi-producer delivery, category registration, panel filtering and state, `.frontier` descriptor decoding, content resolution, and command-line modes. It is included as validation source; its historical check totals are not evidence for the current Project Everest test run.

The older `CompileProof.md` and `PARITY.md` are retained as imported source-branch records from 2026-10-22. Their upstream commit references, `Capture/` masters, historical test totals, and HTML parity claims are not current-branch Project Everest evidence. Current native evidence is only what is listed above and in the Project Everest results.
