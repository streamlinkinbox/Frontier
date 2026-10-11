# Collapsible entities and shared wind components

## Native editor usage

- Use the chevron on folders **or entities with children** to fold their subtrees. Clicking the name selects; the eye controls visibility. Collapsing does not disable rendering or wind.
- Select Cloud, Local Cloud, Local Fog, Height Fog or Aerial Fog. Their native inspectors now begin with **Wind source**.
- Enable **Own wind component** to create an editable Wind child and select that source. Its initial settings copy global wind; subsequent edits are independent.
- Select the Wind child to edit speed, bearing, shear, veer and gust settings in the existing native Wind inspector.
- Choose **Global wind** or any existing component in **Source** to share it. Owning a component and consuming it are separate: an owner may keep its child while using another source.
- Removing a component clears all references to it back to global wind. Removing its owner also cleans up the child and references. Owned children move with their parent and cannot be independently reparented.
- A source's eye and ancestor visibility affect all its consumers. Hiding global wind does not disable independent owned winds. Keep **Follow wind** enabled on volumetric consumers to advect their density.

The dropdown uses canonical owner names (e.g. Local cloud wind), not renamed outliner labels. Renaming or reordering rows does not change source identity. Selection follows stable row keys through component insertion/removal; deleted selections clear rather than jumping to the next row.

## References and rendering

`CelestialSequence` owns five fixed component slots, one per supported cloud/fog owner. Source ID 0 is global; IDs 1–5 address those slots. `ResolveWind(consumer)` returns a `const WindSettings*` into the sequence's storage. Shared users resolve to the same settings, not a copied component or a reference chain. The pointer is valid within the owning sequence's lifetime; resolve again after source removal. These are runtime bindings; project save/load persistence is not added here.

Per-frame value snapshots go to both the CPU `VisibilityRaster`/`VolumetricMedia` path (including medium shadow marching) and the GPU weather uniform. Global cloud, local cloud and local fog each consume their own resolved speed/bearing/shear/veer. Existing global-only callers retain their default behavior.

**Analytic height/aerial fog can own/share wind but has no horizontal noise to advect.** No artificial motion is added to those horizontally homogeneous models. Existing staged ground-cloud shadows remain separate, as documented in the preceding viewport repair. This change does not extend precipitation, fluid or surface-GI wind consumption.

## Current shader ABI

Rebuild the executable and shaders together:

- Weather: **21 vec4 rows / 336 bytes**, at post offset **208**.
- PostConstants: **544 bytes**, binding **24**.
- Rows **18/19/20**: resolved global-cloud/local-cloud/local-fog winds.
- Weather active flag remains at byte **492**; earlier star/flare/rainbow offsets are unchanged.

The 19-row/512-byte records in `Docs/WeatherEvidence` describe the earlier `0eb8a0b` repair, not this ABI. New focused evidence is in `Docs/WindBindingEvidence`.

## Validation

- Full CTest suite: **3/3 pass**, including **155 native scene checks**. Actual mouse-edited fog changes CPU-rendered pixels after scrolling to the control.
- 46 native wind/outliner checks: actual mouse folder/entity chevrons, selection and eye separation, stable selection/collapse after row moves, child inspector exchange, pointer sharing, independent CPU/GPU packs, visibility and safe removal.
- 281 actual shader-source mirror checks in Release and ASan/UBSan, including independently resolved per-medium density. These execute the shader functions as C++/GLM, not on a GPU.
- Weather packer audit passes under ASan/UBSan.
- 22/22 production shaders compile to Vulkan SPIR-V; reflected layout and upload/history/depth/GI-off source guards pass.

Reproduction (using the normal bootstrapped dependencies):

```sh
cmake -S . -B build/wind-native -G Ninja -DFRONTIER_PROOF_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/wind-native -j2
ctest --test-dir build/wind-native --output-on-failure
python3 Exhibits/Workbench/WeatherWiring/RunAudit.py --sanitize
python3 Exhibits/Workbench/WeatherWiring/RunShaderMirror.py --glm .cache/weather/glm --sanitize
python3 Exhibits/Workbench/WeatherWiring/CheckGpuWeather.py --compiler .cache/weather/glslang-build/StandAlone/glslang
```

**Not verified:** Windows linking/application execution, actual Vulkan dispatch, GPU images or timing. The native scene proof renders with the real CPU renderer, not Vulkan.

## Windows acceptance check

```powershell
.\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild
.\Build\Project-Zero.exe
```

1. Collapse/expand Environment, Cloud (with Precipitation), and a cloud/fog entity with an owned Wind. Confirm eye visibility is unaffected.
2. Give Cloud its own wind (e.g. 23 m/s east); give Local Cloud a different wind (11 m/s west). Enable both media and Follow wind. Verify independent motion in the shaded viewport.
3. Set Local Fog's Source to Cloud wind. Edit Cloud's Wind child and verify both consumers change while Local Cloud does not.
4. Collapse Cloud: shared wind must keep working. Hide its eye: shared consumers stop advection. Restore the eye, then remove Own wind: shared consumers fall back to global.
5. Rename and move the owner subtree. Verify collapse, selection and source bindings remain attached to their entities.
