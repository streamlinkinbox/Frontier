# UI Foundation Exhibit

This records the first UI foundation batch. For the current Windows executable, Control Centre, font setup,
and verification status, see `../ControlCentre/README.md`.

## Scope and provenance

Selective port from `SultanAladin/Frontier-` commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
See `Documentation/ImportManifest.json` for source/destination paths and hashes.
This is not a full engine import or a completed native editor.

- `Engine/Interface/`: existing pixel primitives, controls, theme, glyph paths, SVG decoding and ImGui atlas adapter.
- `Engine/Lifecycle/ActivationSequence.h`: new shared lifecycle for controls and scene allocations.
- `UI/Editor/FoundationPanel.*`: small native UI consumer of the lifecycle and imported drawing path.
- `Code/WindowsUI.cpp`: adapted official ImGui Win32/DX11 example, not a new scene renderer.
- `Engine/Render/`: blank apart from a Git directory-retention marker.

The Windows host uses the pinned existing ImGui Win32 and DX11 backends.
The upstream Frontier Vulkan scene renderer and its shaders, lighting, shadows, and visibility code are excluded.
ThorVG software rasterization converts SVG artwork into RGBA textures; this is UI artwork processing only.
All 151 icon symbols use their original SVGs or inherited, fingerprint-checked SVG compatibility variants.
Compatibility substitutions and limitations remain documented in `EngineContent/Icons/ThorVG/manifest.json`.

## Windows reproduction

Prerequisites: Python 3.9+, Git, CMake 3.21+, and a C++20 Windows toolchain with the Windows SDK.
For MSVC, use a Visual Studio x64 developer terminal:

```powershell
python -m pip install -r Automation/FontRequirements.txt
python Automation/PrepareUI.py
python Automation/PrepareFonts.py
cmake -S . -B Build/UI -A x64
cmake --build Build/UI --config Release
ctest --test-dir Build/UI -C Release --output-on-failure
cmake --install Build/UI --config Release --prefix Binaries/UI
cd Binaries/UI
.\FrontierUI.exe
```

Setup downloads checksum-pinned ImGui, ThorVG, and toml++; fonts are prepared separately. Configure never downloads; it verifies preparation.
All six inherited ImGui patches apply before compilation. A repeat setup is idempotent.
Downloaded dependency source stays ignored under `ExternalPackages/`, with its original licences.
MinGW/Clang users must select an appropriate generator/toolchain; the `-A x64` command is MSVC-specific.
No Linux application target is built or packaged. No GitHub Actions are needed or authorised.

## Local checks performed

Editing host: Linux, GCC 12.2.0, Python 3.11.2, CMake/Ninja installed in ignored scratch.
Only local verification tools were built; no Linux product binaries are delivered.

```text
cmake -S . -B _ClaudeScratch/build/ui -G Ninja -DFRONTIER_HOST_VERIFICATION=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build _ClaudeScratch/build/ui
ctest --test-dir _ClaudeScratch/build/ui --output-on-failure
python Automation/ApplyImGuiPatches.py --revert
python Automation/ApplyImGuiPatches.py
python Automation/PrepareUI.py --verify
python Exhibits/UIFoundation/Code/EncodeEvidence.py <build>/evidence/Icons.ppm <output>.png
```

Sanitizer build adds `-fsanitize=address,undefined -fno-omit-frame-pointer` to C++ compilation
and `-fsanitize=address,undefined` to executable linking. All three tests passed in that build too.

| Check                      | Observed result                                      |
|----------------------------|------------------------------------------------------|
| Runtime lifecycle          | Passed with editor hooks compiled out.               |
| Editor lifecycle           | Passed with editor hooks compiled in.                |
| SVG and ImGui validation   | Passed; 151/151 symbols ready and nontransparent.     |
| Cache and ownership        | Reuse, eviction, clear, retained raster checks passed.|
| Invalid/missing artwork    | Explicit errors returned; not silently accepted.     |
| DPI atlas retirement       | Four scales exercised using CPU texture acknowledgements. |
| ImGui patches              | Six apply, verify, skip reapplication, revert/reapply.|
| Address/undefined checking | All three tests passed.                              |
| Windows executable (first batch) | Blocked: no Windows SDK/MSVC or installed cross-toolchain. |
| Windows GPU rendering      | Not run; no Windows runtime/display available.       |

## Visible evidence and limitations

`Images/SVGRasters.png` contains actual ThorVG-produced pixels at 48 px, composited onto a dark background.
It is not a screenshot of a Windows application and does not establish GPU correctness or exact browser parity.
`Code/InterfaceValidation.cpp` generates the pixels and `Code/EncodeEvidence.py` only encodes them losslessly.
`Results/Icons.csv` identifies symbols in image order, left-to-right and top-to-bottom.
`Results/LocalChecks.txt` and `Results/SanitizedChecks.txt` retain test outputs.
`Results/Verification.json` identifies the tested source files by SHA-256 before the commit.

The ImGui test deliberately acknowledges texture requests on the CPU. It does not upload textures to a GPU.
Lifecycle control/geometry classes in the tests are fixtures, not a port of the scene geometry implementation.
The new lifecycle is not retrofitted across all inherited engine classes.
Individual docking patch interaction behaviour is not exhaustively tested by the current command-generation check.
Imported naming/formatting violations remain listed in the project tracker; import is not a compliance waiver.

An attempted LLVM-MinGW toolchain download failed at the restricted release-asset host.
No Windows binary was produced, and no substitute Linux executable is included in the deliverable.

## Ownership contract

Derive from `ActivationSequence`; override only needed content hooks.
Own polymorphic allocations through `ActivationOwnership` so `Finalize()` runs before derived destruction.
Do not delete allocations, release ownership, or reclaim them from inside their own hooks.
Hooks are single-threaded and nonthrowing. Failed activation must roll back its own partial activation resources.
Failed initialization invokes finalization exactly once.
Initialized editor hooks are finalized before runtime resources.
Lifecycle and editor configuration must be consistent within a binary; do not mix `FRONTIER_EDITOR` definitions.

## Notices

The official example's MIT licence is retained in `Code/ImGui.LICENSE`.
See `Engine/Interface/Lucide.NOTICE`, `EngineContent/Icons/Slate-NOTICE.md`, and `Documentation/SourceNotices.md`.
No new licence grant is asserted for inherited Frontier or Slate artwork.

Whitespace checks pass outside the inherited patch payloads. Full `git diff --check` reports unified-diff
context whitespace inside those payloads; they remain byte-identical to the source patches for reproducibility.
