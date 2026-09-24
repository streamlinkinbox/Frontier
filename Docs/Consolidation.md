# Consolidated Frontier checkout

## What changed

The earlier workspace mostly contained additions to an engine that was fetched from `SultanAladin/Frontier-` and rebuilt in `.cache/cpp-sun-full`. That arrangement required several repositories, missing build helpers and an ordered overlay stack before the native editor existed.

The native engine, editor/CAD tool, projects, runtime assets, workbench sources and build infrastructure are now ordinary source files in **this repository**. The existing workspace versions win any import conflict. The imported upstream commit and per-file import decisions are recorded in `ConsolidationImport.json`.

The following existing overlays were folded into imported native source: outliner, Sun/full Sun, Sun lighting/motion, lens flare, atmosphere/sky, moon/catalogue/overview, stars, clouds, fog, weather, camera, main editor integration, Construct, billboards and CMake icon integration. Existing automotive material/host files and other workspace additions were preserved. Browser work stays under `Experimental/FrontierEditor`; the inherited water/ocean/liquid experiments remain separate experiments, not newly integrated runtime systems.

No first-party source download or manual engine patching occurs in the normal build. Historical reconstruction scripts/patches remain for archaeology; their old commands are not the current entry point.

## One entry point

```sh
python3 Tools/Setup.py --verify
```

This downloads five pinned public third-party dependencies, verifies their archive hashes, applies only the existing ImGui third-party divergence, configures CMake, compiles the native editor/renderer and runs tests. It works from the checked-in source rather than any previous proof's absolute-path command JSON.

Use `--all-dependencies` for all 14 libraries needed by the broader native applications, and `--browser` to install/build the browser experiment. GitHub authentication is not needed for these public downloads.

Dependencies are managed archives, not missing submodule pointers. The lock retains known tested pins for the proof dependencies and explicitly identifies newly resolved application-dependency revisions where the original upstream tree omitted a gitlink. These newly pinned revisions are not claimed as full-application execution-verified.

## Build repairs

- Added root CMake presets and a standalone native-proof target that does not require a GPU or Vulkan SDK.
- The proof's runtime data/output is staged in `build/native-proof/evidence`; tests do not overwrite checked-in captures.
- Added shared Python setup with checksum checking, safe extraction, atomic installation and refusal to overwrite unmanaged dependency directories.
- Added the missing GLFW/ThorVG Windows dependency-build helpers and explicit inspector/icon compilation units.
- Corrected the moved Jolt helper's root calculation and call site.
- Replaced the two application build drivers' submodule/TLS-bypass fallback with the same locked setup.
- Source-list parity accounts for `ConstructWorld.cpp` as the separate CPU authoring bridge, not live GPU spawning.
- Added a Linux native-proof/browser CI template at `Tools/CI/verify.yml`. It is not active: the GitHub connection refused workflow creation because it lacks `workflows` permission. An authorized maintainer can copy it to `.github/workflows/verify.yml`. Windows application execution is intentionally not represented as a verified CI target.

## Verification performed

- Native CMake target compiled **directly from this repository**.
- An export of exactly the staged checkout, without ignored dependencies/caches/build outputs, downloaded its own proof dependencies and passed `python3 Tools/Setup.py --verify` end to end.
- Native billboard/inspector/render fixture: **154 checks passed**.
- Bootstrap tests: archive extraction, traversal refusal, link refusal, preservation of unmanaged directories, checksum refusal and immutable lock structure passed.
- All 14 dependency archives installed; offline identity/sentinel check passed on a repeated invocation.
- Consolidated `GameExecution.cpp` syntax check passed.
- CMake/PowerShell application source-list parity passed, including CPU reference and Project-Dyno lists.
- Browser production build passed (1,619 modules); all three pages and 151 native SVG authorities passed the layout check.
- SolidArc executable and verification targets compiled; **all 55 verification cases passed**. The sandbox interrupted the first test invocation during case 29; cases 29–55 were then rerun successfully.

See `ConsolidationVerification.txt` for collected output and the clean-checkout check when present. The CI template can repeat the primary native and browser checks once an authorized maintainer enables it; local results are not presented as already-completed GitHub CI.

## Boundaries

- Full Windows/Vulkan and full Linux Vulkan application execution remain unverified. Platform SDKs, graphics drivers and window-system packages still must be installed locally.
- The native billboard fixture proves the CPU weather/render path, not full GPU weather parity. No additional weather feature or fluid integration is claimed by moving source into one repository.
- This is a source consolidation, not a merge of upstream git histories. Provenance remains documented; no source repository was deleted or renamed.
- Existing source/asset/font notices are retained. No overall licence grant was found in the imported Frontier snapshot or the existing Slate import; consolidation does not invent one.
- Large untracked historical screenshot bundles remain in the local workspace but are excluded from new Git additions. Previously committed automotive showcase evidence is retained. Runtime assets and the current compact native billboard evidence are included. New captures are generated by the native proof.
- Downloaded dependencies, compiler output, local authored scenes and credentials are not committed.
