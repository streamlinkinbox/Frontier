# Fluid editor

🧩 The Fluid editor of `Experimental/Fluid` (the prototype's "Fluid — Frontier Experimental"): its catalogue, its Pyro solver and, as the steps below land,
its window inside the Frontier editor, built on the editor's own controls. **Stage 1 of 4 is here: the catalogue, the camera and the solver, checked.**

## Where it came from

The work was brought in from `c7egoist/Frontier`, branch `arena/a0311b9c-frontier` at `d7ac487` (read in a separate clone; nothing of this repository was
cleared or replaced). That repository is a standalone slice with a small engine of its own. Its Fluid editor, its Pyro solver and its catalogue work (its own
proofs run here on SwiftShader), but it also made a second editor, hand-drew its controls and moved the tyre smoke with the car. Part by part:

| Their part | Here | Why |
|---|---|---|
| `Experimental/Fluid` (the HTML Fluid editor, from `eosclient0001-rgb/Frontier@33c5f2e`) | `Experimental/Fluid`, unchanged | the reference every number below is checked against |
| `GasPresetLibrary.h`, `GasSceneFields.inl`, `FluidEditorControls.h` (generated) and their generators | kept; generators in `Automation/`; `--check` passes against `presets.js` and `SceneSpecification.js` | 18 presets, 85 settings, 82 controls |
| `GasSceneCodec.h` | kept | the `.gasscene.toml` format the prototype exports |
| `PyroVolumeSolver`, `VulkanContext`, the SPIR-V header | kept; the orbit camera moved out into `PyroCamera.{h,cpp}` (no Vulkan in it, so the interface can use it) | the working Pyro solver |
| `FluidEditorWorkspace`, `TypographyKit`: the hand-drawn Fluid editor | **not brought** | 57 direct `ImDrawList` drawing calls and no ImGui widget at all: its own sliders, dropdowns, popups and hit tests (`ClickedInRect`, `DrawSelectBox`, `ActiveSliderKey`); the window is rebuilt on the editor's controls (stage 2) |
| `FrontierEditorWorkspace`, `WorldSdfScene`: a second Frontier editor and its own world | **not brought** | the Frontier editor exists; the Fluid editor opens from it |
| `TyreSmokeIntake` | **not brought** | repeats `PhysicalDynamics/Vehicle` (Pacejka, thermal) and `FluidDynamics/PlumeSourceProjection`, and re-anchors its domain on the car every frame (`FrontierEditorWorkspace.cpp:429`); see below |
| Session, Lobby, Input | untouched | not among the faults, and this repository already has them |
| `.github/workflows/everest-binaries.yml` | **not brought** | no workflows without the owner's approval |

## The smoke

In the prototype the tyre smoke is solved on **a world grid**: `PlumeSpace.js` keeps toroidal windows that follow the car, and `RecentreWindow` moves a
window only in whole cells, and only when the car leaves the middle of it. A cell keeps its place in the world (its address is the world cell modulo the
window), so smoke is not carried by the car. The imported solver has no such window: the other agent's editor set the domain's origin to the car's position
every frame, so every cell of the lattice moved with the car and the smoke went with it. The vehicle's smoke here is the plume already ported from
`PlumeSpace.js` (`Rendering/PlumeSimulation`, checked against the prototype, relocations included); the Fluid editor's volumes are boxes fixed in the world.

## What was run

| Check | Program | Result |
|---|---|---|
| The catalogue, the codec, the camera and the solver on a device | `PyroVolumeValidation --require-vulkan` | 🟢 34 assertions, 0 failed, 8 s on SwiftShader |
| Generated files against the prototype | `GenerateGasPresets.py --check`, `GenerateGasSceneCodec.py --check`, `GenerateFluidEditorControls.py --check` (in `Automation/`) | 🟢 all three match |

`PyroVolumeValidation` asserts: 18 presets and 18 cards covering them; 85 settings spelled by the codec; 82 controls, each of which reads and writes inside its
range (63 ranges to both ends, 13 toggles, 6 choices to each option); all 18 presets round-tripped through `.gasscene.toml` with all 85 settings equal; the
8 sample scenes read with no unknown setting; the refusals (another format, another version, no name, a float for a voxel count) and the note for an unknown
setting; the camera (unit frames at right angles, 0.0065 rad a pixel, poles, zoom limits, pan, the ray through the middle, the four views); and on a device the
15 pipelines, the lattice (32 and 48 cells), twenty steps, a rendered volume with warm bright pixels, the same volume gone after a clear, a burst that
leaves the domain no smaller, a 2 × 2 flipbook, and a second solver given the same steps rendering the same picture byte for byte.

## Reproduce

```sh
export PATH=$HOME/.local/bin:$PATH
cmake -S . -B _ClaudeScratch/build/UI -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DFRONTIER_HOST_VERIFICATION=ON -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF
ninja -C _ClaudeScratch/build/UI PyroVolumeValidation
SS=ExternalPackages/VulkanCompute/SwiftShader
FRONTIER_VULKAN_LIB=$SS/libvulkan.so.1 VK_ICD_FILENAMES=$SS/vk_swiftshader_icd.json LD_LIBRARY_PATH=$SS _ClaudeScratch/build/UI/PyroVolumeValidation --require-vulkan
python3 Automation/GenerateGasPresets.py --check && python3 Automation/GenerateGasSceneCodec.py --check && python3 Automation/GenerateFluidEditorControls.py --check
```

Prerequisites: the dependencies of `Automation/Prepare*.py`, `node` 22 (the controls generator reads `SceneSpecification.js`), a Vulkan loader (SwiftShader here).
Tested on `d9f7be1` plus the commit that adds this file (uncommitted while it ran), Debug, GCC 12.2.

## Not done, and not checked

- ⚠️ The solver and its context are the other agent's: GLSL 450 programs compiled to SPIR-V by `Automation/GenerateVulkanShaders.py` (needs `glslang`, not run here, so
  `VulkanSpirvLibrary.h` is unchecked against its generator), on a Vulkan context of their own. They are not yet Slang programs on `DeviceExchange`, as the rest of the
  engine's are, and the naming of the imported files was not reviewed against `SKILL-Naming.md`.
- 🚧 Stages 2 to 4 (the window on the editor's controls, the record and its presenter, the captures) are not in this commit.
- Timings are this sandbox's CPU under SwiftShader and say nothing about a GPU.
