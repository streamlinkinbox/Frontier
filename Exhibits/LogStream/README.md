# Log Stream validation

Headless proof for the first C++ port: the portable diagnostics engine, the
Log Stream panel's view-model at behavioural parity with the approved HTML
prototype (`Frontier/Projects/Project-VehicleHmiDemo/Reference/LogStreamPanel.html`),
project descriptor decoding + browser enumeration, the reference-not-copy
content resolution, the launch command line, and the Project Zero code image
against a recreated v3 interchange contract (`Support/ProjectInterchange.h`
— scaffolding that mirrors the committed Project-Networking implementation
until the host SDK header is supplied).

## Build & run

CMake (any platform; needs toml++ headers):

```sh
cmake -S . -B Build -DFRONTIER_TOMLPLUSPLUS_DIR=<path-to-tomlplusplus>
cmake --build Build
ctest --test-dir Build --output-on-failure
```

Or compile directly (what the sandbox run does):

```sh
g++ -std=c++17 -Wall -Wextra -I ../.. -I <tomlplusplus>/include -I Support \
    ../../Engine/Diagnostics/*.cpp \
    ../../Engine/Interface/{ProjectContent,ProjectDescriptor,LogStreamPanel,LogStreamPanelPresentation}.cpp \
    ../../Applications/Editor/{CommandLine,ProjectBrowserHost}.cpp \
    ../../Projects/Project-Zero/Source/ProjectZeroInterchange.cpp \
    LogStreamValidation.cpp -pthread -o LogStreamValidation
./LogStreamValidation ../..   # repository root, for the real descriptors
```

## What it proves / does not prove

Proved by execution: ring overwrite + loss accounting, multi-producer
contention without torn or duplicated delivery, category registration +
palette fidelity, panel filters/ceilings/queries/collapse/pause/follow/
clear at prototype parity, all repository `.frontier` descriptors decoding,
content shadowing rules, command-line modes, interchange negotiation and
first diagnostics, shipping-profile compile-time facts.

Not proved here: an opened OS window (GLFW/Vulkan presentation on the
Windows/MSVC/Vulkan SDK build). The SDK-side translation units are guarded
behind `FRONTIER_WITH_INTERFACE_SDK`; they have since been compiled against
the genuine upstream headers and rendered headlessly — see `CompileProof.md`
and the PNG masters in `Capture/` (13 frames: panel close-ups at the
prototype's 1240x830 and the real TextureWorkspace composition —
`ControlCentrePanel` notch over a host-owned `PanelDockspace` with Viewport |
Log Stream at 50/50 — at 1920x1080). The
panel's drawing is an exact 1:1 port of the approved prototype with the
upstream dropdown + dock frame reused; the element-by-element table is in
`PARITY.md`. Current validation total: 69 checks.
