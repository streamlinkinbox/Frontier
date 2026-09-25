# Native icons: ThorVG-only repair

## What was broken

The strict native SVG gate rejected **69 of the 151 registered icons**. The old atlas test explicitly expected 82 ready / 69 blocked, so these X placeholders were reproducible failures, not missing original SVG files. An older optional browser-rasterized `.rgba` fallback also conflicted with the requirement to render icons through ThorVG.

## Current path

- All 151 icons render through the pinned ThorVG 1.0.0 software renderer. **No runtime baked-icon fallback.** A valid legacy FIB1 bake is deliberately ignored by a regression test.
- The 82 admitted originals are used directly. The other 69 use editable SVG-only compatibility artwork in `EngineContent/Icons/ThorVG/`; original artwork is unchanged.
- Variants carry an FNV-1a-64 fingerprint of their original source. Missing/stale variants, missing originals, unsupported variant elements and malformed SVGs fail visibly rather than falsely reporting success. This checks freshness of trusted local assets, not security/authenticity. Assets remain cached until `IconArt::Clear()` or process restart.
- The native ImGui texture atlas is still necessary for drawing. Its pixels are generated **at runtime by ThorVG**, not read from pre-baked icon images.
- Default asset lookup checks the working directory, then the executable directory and its ancestors on Windows/Linux. Explicit custom roots are not silently replaced. The Windows toolchain and Project-Zero CMake target stage originals and vector variants beside the executable. Failure diagnostics include missing paths and are printed when an atlas contains substitutes.

## Compatibility differences — not browser-pixel equivalence

The pinned loader cannot render the entire browser SVG feature set. These variants deliberately preserve the main vector geometry and colors while making these documented adaptations:

- Drop shadows: cloned vector silhouettes, alpha-preserving gradient copies and supported Gaussian blur; filter bounds/compositing may differ.
- Labels: outlined paths using bundled OFL **Archivo**, rather than a runtime Arial/system-font dependency. Label text stays unchanged; metrics and glyph shapes can differ.
- Fabric: repeating weave expanded into clipped vector strokes.
- Procedural turbulence, displacement, color-matrix and composite filter primitives: omitted. Some surfaces/edges therefore lose procedural texture/detail. Existing gradients, silhouettes and supported blur remain.
- SVG2 `rgba()` paints: converted into RGB plus explicit opacity. Without this conversion the pinned loader rendered the Spotlight beam black.
- Smoke: its gradient-filled mask rendered blank in this pin. The same fade is approximated with 64 fine vector-opacity strips, not a raster texture.

Per-file changes and source fingerprints are recorded in [`manifest.json`](../EngineContent/Icons/ThorVG/manifest.json). Legacy browser bake utilities/assets remain for historical exhibits, but `IconArt` no longer includes or calls their loader. [The old celestial bake report](CelestialOutlinerIcons.md) is marked superseded.

## Verification

Linux GCC 12.2, actual pinned ThorVG + repository-patched ImGui:

| Proof | Release | Debug + ASan/UBSan (including ThorVG) |
|---|---|---|
| IconArt | 793 checks PASS | 793 checks PASS |
| Native ImGui atlas | 5,791 checks PASS | 5,791 checks PASS |

- **151 ready, 0 unsupported, 0 substitutes.** Nonempty artwork checked at 24, 30, 48 and 96 pixels. Individual 128-pixel outputs also generated.
- Atlas compares every icon against direct ThorVG output at 1× and 2×, including UVs, straight alpha, tint and clipping. DPI churn remains bounded to two atlases, with context teardown/lifetime checks.
- Missing/malformed/stale assets, unsupported fixtures, invalid sizes, cache eviction and retained raster ownership tested.
- Tests run from an unrelated working directory using staged executable-relative SVGs; a separate `/tmp` launch also passed ancestor lookup.
- Regeneration produced identical variant bytes and manifest on repeat.
- [Per-icon results](IconEvidence/ThorVGResults.tsv) and [rendered proof sheet](IconEvidence/ThorVGAllIcons.png). The proof PNG is documentation, **not a runtime asset**.

**Not verified here:** the complete Windows application build or a real GPU-backed ImGui frame on the user's PC. The Windows staging and lookup code is implemented; Linux native CPU tests do not establish Windows/GPU execution.

## Rebuild on Windows

From the repository root:

```powershell
.\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild
.\Build\Project-Zero.exe
```

Use `-FluidOpenMP` too if that is part of your usual fluid build. Keep `EngineContent/Icons/` beside the executable when distributing it, including its `ThorVG/` subdirectory. Do not substitute `Baked/` files.

## Reproduce the focused proofs

After bootstrapping dependencies, using CMake 3.21+ and Ninja on PATH:

```sh
python3 Tools/Bootstrap.py --profile all
cmake -S Exhibits/Workbench/IconArt -B build/icon-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/icon-release -j2
ctest --test-dir build/icon-release --output-on-failure

cmake -S Exhibits/Workbench/IconArt -B build/icon-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug -DICON_SANITIZERS=ON
cmake --build build/icon-sanitized -j2
ctest --test-dir build/icon-sanitized --output-on-failure
```

The sanitizer option requires GCC/Clang. Outputs are under the selected build directory, not source control. To regenerate vector artwork after an original changes:

```sh
# Install in a development virtual environment; not a runtime dependency.
python -m pip install fonttools==4.66.0
python Tools/Build/MakeThorVGIcons.py
# Then rerun the C++ proofs and review artwork/manifest changes.
```

Optional evidence sheet (requires Pillow): `python Exhibits/Workbench/IconArt/MakeProofSheet.py` after the Release proof run.
