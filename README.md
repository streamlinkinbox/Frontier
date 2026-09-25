# Frontier

**One repository for the native engine, editor, projects, runtime assets and browser experiments.**

The engine/editor sources are checked in directly. You do **not** need to clone another Frontier repository, find missing source files or apply the old engine patch chain. Third-party libraries are downloaded automatically from an immutable, checksummed dependency lockfile.

## Start here

Prerequisites: **Python 3.11+, Git, CMake 3.21+, Ninja and a C++20 compiler**. On Windows, use a Visual Studio x64 developer terminal with the C++ desktop workload installed.

```sh
git clone https://github.com/streamlinkinbox/Frontier.git
cd Frontier
python Tools/Setup.py --verify
```

On systems where Python is named `python3`, use that instead. No GitHub account/token is needed for the public dependency downloads. The first run downloads libraries; subsequent runs reuse them.

`--verify` builds the **actual native editor and CPU scene-rendering proof**, then runs the billboard/inspector synchronization and setup-safety tests. It does not launch the Vulkan game. Generated captures and the test log go to:

```text
build/native-proof/evidence/
```

For setup without compiling, run `python Tools/Setup.py`. To install all application/import/physics dependencies as well, add `--all-dependencies`.

## What lives where

| Directory | Contents |
|---|---|
| `Engine/` | Native engine, renderer, shaders, inspectors, billboards and authoring support |
| `Editor/AuthoringTools/Modelling/SolidArc/` | Native CAD/modelling tool and its verification suite |
| `Projects/Project-Zero/` | Main application, native project integration and CPU reference hosts |
| `Projects/Project-Dyno/` | Dyno/audio project |
| `Projects/Project-Fluid/` | Flux particle fluid, Ripple pond and CPU tests; [integration/review](Docs/WaterIntegration.md) |
| `EngineContent/` | Runtime icons, font licences, fonts, celestial textures and star data |
| `Experimental/FrontierEditor/` | Browser editor prototype, icon gallery and collection studies |
| `Experimental/{Liquid,Ocean,Water}/` | Separate inherited research experiments—not integrated runtime features |
| `ExternalPackages/Dependencies.lock.json` | Exact third-party revisions and archive SHA-256 checksums |
| `Tools/` | Dependency setup, build helpers and checks |
| `Exhibits/Workbench/` | Native proof source and research harnesses |
| `Exhibits/Gallery/NativeBillboards/` | Checked-in native billboard/render evidence |
| `Docs/` | Architecture, audits, provenance and historical notes |

## Native application — Windows

In addition to the prerequisites above, install the **Vulkan SDK** and a Vulkan-capable graphics driver. `VULKAN_SDK` must point to the SDK.

```powershell
python Tools/Setup.py --all-dependencies
powershell -NoProfile -ExecutionPolicy Bypass -File Projects/Project-Zero/Build/ToolchainSequence.ps1 -Development -Run
```

The build driver uses this checkout's source and locked dependencies. GLFW and ThorVG build helpers are included; Jolt's helper path is corrected. No recursive submodule checkout or disabled TLS verification is required.

**The complete Windows/Vulkan application has not been executed in this Linux environment.** The CPU proof passing is not a GPU-weather-parity claim. The native CPU render/inspector checks are the verified path; see [consolidation status](Docs/Consolidation.md) for remaining boundaries.

## Native application — Linux

Install the Vulkan SDK, GLFW's X11/Wayland development prerequisites and a shader compiler (`glslc` or compatible `slangc`), then:

```sh
python3 Tools/Setup.py --all-dependencies
cmake --preset linux-app
cmake --build --preset linux-app
```

The full Linux Vulkan application is not yet an executed verification target here. Use the `native-proof` preset for the tested headless path.

## Browser editor and galleries

Requires **Node.js 20.19+ / npm**. The browser experiment is separate from the native editor, but included in this repository.

```sh
npm --prefix Experimental/FrontierEditor ci
npm --prefix Experimental/FrontierEditor run dev -- --port 5173
```

Open the displayed URL. Pages: `/`, `/icons.html`, `/collection-icon-options.html`.

```sh
npm --prefix Experimental/FrontierEditor run build
```

Or install/build it alongside native setup with `python Tools/Setup.py --browser`.

## SolidArc

```sh
cmake -S Editor/AuthoringTools/Modelling/SolidArc -B build/solidarc
cmake --build build/solidarc --target SolidArc SolidArcVerification --parallel 3
ctest --test-dir build/solidarc --output-on-failure
```

## Reproducibility and provenance

- [Consolidation details and verification](Docs/Consolidation.md)
- [Source/dependency notices](NOTICE.md)
- [Dependency setup](ExternalPackages/README.md)
- [Optional CI template](Tools/CI/README.md) (not yet activated)
- [Native billboards and renderer evidence](Exhibits/Workbench/Billboards/Native.md)

Older research documents describe the former multi-repository arrangement. Historical patches and reconstruction scripts are preserved for provenance; **they are not the normal setup/build route**. Start with the commands above. Large untracked historical screenshot bundles are not needed to build; previously committed automotive evidence is retained; new proof captures are generated in `build/`. Browser design history is preserved in [Docs/BrowserEditorHistory.md](Docs/BrowserEditorHistory.md).

## Water prototype

Flux/Ripple are included with a tested **load-time Ripple mesh bridge**, not live main-engine fluid simulation. Build/test with `cmake --preset fluid-cpu`, `cmake --build --preset fluid-cpu`, then `ctest --preset fluid-cpu`. The rebuilt Project-Zero accepts `--water-body-snapshot`. See [scope, commands, measurements and limitations](Docs/WaterIntegration.md).
