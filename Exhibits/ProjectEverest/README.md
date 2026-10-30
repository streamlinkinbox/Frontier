# Project Everest — native UI exhibit

The browser, application-selection dialog and workspace below are rendered by production C++ UI code.
These are CPU-rasterized ImGui draw lists, not browser mockups or generated illustrations.

## Workspace

![Project Everest with the production LogStream](Images/Workspace.png)

## Browser and application selection

![Everest registered in the shared browser](Images/Browser.png)

![VehicleHMI application selection](Images/ApplicationSelection.png)

## Shared Control Centre and themes

![Canonical Control Centre appearance page](Images/ControlCentre.png)

![Light theme applied to workspace and LogStream](Images/LightTheme.png)

## Verification

| Check                                      | Current result                                    |
|--------------------------------------------|---------------------------------------------------|
| Native C++ target                          | Built with GCC 12.2, C++20, Linux x86-64           |
| Focused CTest                              | 1/1 passed; see `Results/CTest.txt`                |
| Browser                                    | Everest searchable, selectable                    |
| Application selection                      | Real mouse press/release activates launch button  |
| Handoff                                    | TOML decode; correct path; consumed exactly once   |
| Workspace registration                     | Repeated configuration does not duplicate panels  |
| LogStream                                  | Existing native panel registered; close/reopen    |
| Docking / Control Centre / theme            | Actual shared native draw output captured         |
| Native Windows windowing / Vulkan display   | Not run                                           |
| Vehicle scene / ray tracing / ReSTIR         | Not connected or tested in this UI milestone       |

Configuration: default CMake configuration (no explicit optimization profile), host verification,
`FRONTIER_EVEREST_EXHIBIT_ONLY=ON`. Test source and captures are retained together here.
`Results/CaptureHashes.json` identifies the exact capture source and images. `Results/Build.txt` retains the final
incremental build result. Tested work is the uncommitted Project Everest patch over session origin `68f3016`.
Canonical UI baseline: `71e4a17`; LogStream baseline: `f97b991`. The native Windows application already uses
`EditorWorkspace::ConfigureProject`, which activates the added Everest panels after decoding the launch path.
The isolated `FrontierUI` exhibit entry also propagates the project summary.

## Reproduce

Prerequisites: GCC / Clang C++20, Python 3.9+, CMake 3.21+, Ninja, network access to the pinned GitHub dependencies.
Use a Python virtual environment; `GH_TOKEN` can come from existing local `gh` authentication if API limits apply.

```bash
python -m venv .venv
.venv/bin/pip install cmake ninja pillow -r Automation/FontRequirements.txt
export PATH="$PWD/.venv/bin:$PATH"
python Automation/PrepareUI.py
python Automation/PrepareFonts.py
cmake -S . -B build/everest -G Ninja \
  -DFRONTIER_HOST_VERIFICATION=ON -DFRONTIER_EVEREST_EXHIBIT_ONLY=ON \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/everest --target ProjectEverestValidation -j 4
ctest --test-dir build/everest --output-on-failure
python Exhibits/ProjectEverest/Code/ConvertCaptures.py
```

The default desktop configure was attempted but blocked by missing wayland-scanner and X11 development headers.
The focused exhibit option intentionally returns before GLFW/platform targets; it still builds and draws the real
shared engine, editor and LogStream code. It does not prove native window dragging, detached platform windows,
GPU presentation, renderer performance or Windows runtime correctness. No binaries are delivered.
The browser preview is deliberately unavailable here. No vehicle image has been substituted.

## Follow-up

See [UI sequence and lifetime contract](../../Documentation/Plans/Ongoing/ProjectEverest.md).
Review these exhibits before adding Outliner / Inspector or starting renderer integration.
