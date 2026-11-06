# Fluid editor

🧩 The Fluid editor of `Experimental/Fluid` (the prototype's "Fluid — Frontier Experimental") as a window of the Frontier editor, built on the editor's own
controls: an Outliner of the volume's parts, a viewport with the Pyro solver's picture, its transport and a Log Stream, an Inspector for the part selected and a
sheet of the 18 presets. In Project Everest it opens from a **Fluid Volume**: Construct (+) → *Fluid Volume* (Weather group) → select it → **Fluid Editor** in its
Inspector.

## Where it came from

The work was brought in from `c7egoist/Frontier`, branch `arena/a0311b9c-frontier` at `d7ac487` (read in a separate clone; nothing of this repository was
cleared or replaced, and nothing was pushed to theirs: the clone is read-only). That repository is a standalone slice with a small engine of its own. Its Fluid
editor, its Pyro solver and its catalogue work (its own proofs run here on SwiftShader), but it also made a second editor, hand-drew its controls and moved the
tyre smoke with the car. Part by part:

| Their part | Here | Why |
|---|---|---|
| `Experimental/Fluid` (the HTML Fluid editor, from `eosclient0001-rgb/Frontier@33c5f2e`) | `Experimental/Fluid`, unchanged | the reference every number below is checked against |
| `GasPresetLibrary.h`, `GasSceneFields.inl`, `FluidEditorControls.h` (generated) and their generators | kept; generators in `Automation/`; `--check` passes against `presets.js` and `SceneSpecification.js` | 18 presets, 85 settings, 82 controls |
| `GasSceneCodec.h` | kept | the `.gasscene.toml` format the prototype exports |
| `PyroVolumeSolver`, `VulkanContext`, the SPIR-V header | kept; the orbit camera moved out into `PyroCamera.{h,cpp}` (no Vulkan in it, so the interface can use it) | the working Pyro solver |
| `FluidEditorWorkspace`, `TypographyKit`: the hand-drawn Fluid editor | **not brought**; the window is `Frontier/Engine/Interface/FluidEditor/*` on `Interface/Controls/*` | 57 direct `Draw->Add*` calls in `FluidEditorWorkspace.cpp` and no ImGui widget at all (ImGui only reads the mouse and keys there and hands out the draw list): its own sliders, dropdowns, popups and hit tests (`ClickedInRect`, `DrawSelectBox`, `ActiveSliderKey`) |
| `FrontierEditorWorkspace`, `WorldSdfScene`: a second Frontier editor and its own world | **not brought** | 32 more `Draw->Add*` calls and no widget; the Frontier editor exists, and the Fluid editor opens from it |
| `TyreSmokeIntake` | **not brought** | repeats `PhysicalDynamics/Vehicle` (Pacejka, thermal) and `FluidDynamics/PlumeSourceProjection`, and re-anchors its domain on the car every frame (`FrontierEditorWorkspace.cpp:429`); see below |
| Session, Lobby, Input | untouched | not among the faults, and this repository already has them |
| `.github/workflows/everest-binaries.yml` | **not brought** | this repository has its own `EditorPreviewWindows.yml`, run only on the owner's approval (to build Frontier and Project Everest for testing) |

## What was reported, and what was found

| Reported | Found (in their repository unless said) | Here |
|---|---|---|
| The UI looks very wrong; reuse the code, especially the sliders and panels | `FluidEditorWorkspace.cpp` 57 and `FrontierEditorWorkspace.cpp` 32 `Draw->Add*` calls; no `ImGui::Button`, `Slider`, `Combo` or `Checkbox` in either (ImGui only reads the mouse and keys and hands out the draw list); an orange accent of their own | the window is made of the editor's `Controls/*`: `SectionRows::Amount` for the 63 ranges, `SectionRows::Switch` for the 13 toggles, `SelectRow` for the 6 choices, `TabStrip`, `MetricTiles`, `DisclosureRow`, `ActionRow`, `PageHeading`, `CaptionText`, with the editor's palette and typefaces |
| The smoke in the HTML is on a world grid, so it is continuous and not attached to the vehicle | `FrontierEditorWorkspace.cpp:429` sets `Plume.OriginWorld` to the car's position every frame (the tyre domain `FollowsVehicle`, 48³ over 7.6 m); `PyroVolumeSolver` has no relocation, so every cell moves with the car | not brought. The vehicle's smoke here is the plume ported from the prototype's `PlumeSpace.js` (toroidal windows that recentre only in whole cells: `PlumeSchedule.cpp:115`, `SmokeSequence.cpp:70`; checked against the prototype in `Exhibits/PlumeSimulation`); the Fluid editor's volumes are boxes fixed in the world |
| It may have made its own editor while the Frontier editor exists | its own editor, its own `VulkanContext`, `TyreSmokeIntake`, `WorldSdfScene` and `VulkanImGuiPresenter` | not brought; the Fluid editor is a window of the Frontier editor (a `FluidVolume` record, an Inspector, a presenter the host registers) |
| (Standing) a 1:1 port | the solver is not 1:1 in places; see the next table | fixed where checked, open where listed |

### The port against the prototype

| What | The other agent's | Here | Proved by |
|---|---|---|---|
| Burst patterns and their delay clocks | patterns approximated, one clock | the prototype's single, shrapnel, salvo, collide and mushroom with their own clocks | `PyroVolumeValidation` (47 then) and three mutants stopped (`c3b2698`) |
| Opening settings | the preset table's base alone | `InitialParameters` of `SceneSpecification.js` (seven overrides: grid, quality, overlay…) | 18 presets × 85 settings against `Reference/PresetParameters.toml`, which `DumpPresetParameters.mjs` writes from the prototype's own module; `MutateGasCatalogue.sh` (`a7a286e`) |
| `voxelQuantization` | counted as an integer: 0.88 became 0 | a fraction | the same table |
| Orbit camera and pointer | 0.0065 rad/px, other limits, a copied number set | 0.0075 rad/px, polar limits 0.12 and π−0.08, zoom ×(1+δ·0.001) in [0.85, 9.5], pan `dist·0.0014`, easing `min(1,16dt)`, the camera-plane hit, the turntable, flamethrower and detonate as `camera.js` | `Reference/CameraTrace.toml` (180 steps, tolerance 2e-4), which `DumpCameraTrace.mjs` writes by running `camera.js` itself; `MutateCamera.sh` |

**Still not the prototype's** (open, in the order they would show): the time step (the prototype takes `min(raw, 0.033) × timeScale`; the solver clamps to
[1/240, 1/24] and the workspace passes `min(Delta, 0.033)`); the surge's dynamics (the prototype's target model: rise 2.8, fall 0.45, decay 0.22 a second,
+0.65 for a blast and +0.75 for shrapnel, against `PyroVolumeSolver.cpp:699-705`); the bounds box (`getBoundsBox`: `surgeMult = 1 + surge·(max−1)`,
`effH = H·(1 + (surgeMult−1)·1.15)`, against `ComputeBoundsBox`); shrapnel (jitter, a life of 1.55–2.2 s, gravity 1.28, the floor's bounce); the shockwave shell
(`shaders-glsl.js` 765-768, 961-975 and 1018-1021 are not in the C++ GLSL); the floor grid and the voxel lines (the solver ignores `FloorLatticeShown`,
`VoxelLatticeLinesShown` and the bounding box's flag); the `#object-name` rename. None of it was started here.

## The window

`Interface/FluidEditor/*` and `Interface/FluidVolumeEditor.{h,cpp}` (editor only, under `FRONTIER_EDITOR`):

- **Outliner** (292 px): the counts of parts enabled and disabled, *Presets*, the parts grouped (Gas: the domain and its emitter or collider; Light: the sun),
  an eye on each, and *Add to scene* (Fire, Smoke, Sphere, Cylinder, Slab).
- **Viewport**: *Scene view* with Perspective, Front, Side and Top, fit, full screen, restart and the library; the tools (orbit, flamethrower, detonate); the
  solver's picture; the transport (restart, pause or play, step, the timecode, the speed, *Single burst*, *Trigger*), the ruler, and the **Log Stream** below.
- **Inspector** (404 px): the part's tabs (Source, Simulation, Rendering), the tiles (lattice, steps, time), the groups as disclosure rows with the 82 controls of
  the generated catalogue.
- **Presets**: a sheet of 18 cards with the prototype's filter chips and a search; a card sets all 82 controls, restarts, selects the emitter (else the domain),
  fits the camera and sets the preset's burst off when the prototype does on load.

The picture comes from `PyroSolverSession` (`Extensions/Fluid`: its own headless `VulkanContext`, a picture of the viewport times `RenderScale`, read back as
RGBA), which `FluidVolumeExtension` hands to the app's device as a texture through `DeviceExchange`; `Experimental/Frontier/Native/WindowsFrontier.cpp` registers
the presenter. Without a solver the window still edits (`FluidEditor-NoSolver.png`).

## What was run

| Check | Program | Result |
|---|---|---|
| The catalogue, the codec, the camera and the solver on a device | `PyroVolumeValidation --require-vulkan` | 🟢 56 assertions, 0 failed (8 s on SwiftShader, Debug); the 32 that need no device pass without one |
| The generated files against the prototype | `GenerateGasPresets.py`, `GenerateGasSceneCodec.py`, `GenerateFluidEditorControls.py` with `--check` | 🟢 all three match |
| The catalogue against mutants | `MutateGasCatalogue.sh` | 🟢 3 of 3 stopped |
| The camera against mutants | `MutateCamera.sh` | 🟢 10 of 10 stopped (orbit speed, zoom limit, ray depth, polar limit, pan speed, zoom speed, turntable, easing, focus distance, reset distance); the control passes |
| The window driven as a person drives it: the real window, controls and solver | `FluidEditorCapture` | 🟢 28 claims, 0 failed; 8 captures (10 min on SwiftShader, Debug) |
| The Smoke & fire window, which shares `Controls/WindowControls.h` and Everest's list of record types | `FluidPlumeEditorValidation` | 🟢 588 assertions, 0 failed (42 s). The count did not change with the Fluid wiring and the program does not test the Fluid editor: it shows the plume's window is unharmed |
| `WindowsFrontier.cpp`'s three registrations (Particle, Smoke & fire, Fluid) against the real headers | `Exhibits/FluidPlumeEditor/Code/VerifyWindowsRegistration.sh` | 🟢 type-check (`-fsyntax-only`); 6 broken copies rejected |
| A Windows build, its package and a start | GitHub Actions run [`42210475527`](https://github.com/streamlinkinbox/Frontier/actions/runs/42210475527) on `e19671c` | 🟢 Zig cross-build and package; 🟢 the package started on Windows with a software Vulkan driver (the Project Browser and Project Everest's Scene Viewport reached); 🔴 MSVC build (see below); the pre-release [`editor-preview-e19671c2`](https://github.com/streamlinkinbox/Frontier/releases/tag/editor-preview-e19671c2) was published |

`FluidEditorCapture` makes 28 claims: the opening state (the emitter on Source, the aim at 4.1 from (0, 0.3, 0), all 82 controls as the prototype's
`InitialParameters`); the solver runs and the picture follows the viewport; pause, step, play and restart; orbit, zoom, pan and the views as `camera.js` has them;
the detonate tool and the source brush; the library (18 cards, Fire narrows it to 7), a preset card (0 of 82 controls differ) and the preset running; the
Domain and Sun rows opening on Simulation and Rendering; the light's eye; a disabled volume that takes no step and still draws; a record written to TOML and read
back (0 of 82 differ); a 1920 × 1080 window; and no solver (the window says why and still edits). Its first run failed two claims, both the harness's own
expectations (a restart lands one refresh after the press; a pan uses the camera's eased aim): they were corrected, not the product. Its camera numbers restate
`camera.js`'s arithmetic; the camera's authority is `Reference/CameraTrace.toml`, which `camera.js` itself wrote.

Pictures (`Images/`, the window at 1366 × 800 unless said; the solver's picture is 443 × 290 at a render scale of 0.8):

| Picture | Shows |
|---|---|
| `FluidEditor-Boot.png` | the opening state: the emitter on Source, the solver's fire and smoke in a 48³ domain, the transport, the ruler and the Log Stream |
| `FluidEditor-Burst.png` | a burst placed with the detonate tool |
| `FluidEditor-Library.png` | the sheet of 18 presets with its chips and search |
| `FluidEditor-Preset.png` | the preset "Tactical C4 / HE Blast Wave" (`tactical_ordnance`) loaded and running on a 96³ lattice, the domain's Simulation page |
| `FluidEditor-Domain.png` | the domain's Simulation page with its tiles and sliders |
| `FluidEditor-Emitter.png` | the emitter's Source page (disabled by the preset) after the light's eye was toggled |
| `FluidEditor-1080p.png` | a window of 1920 × 1080, a picture of 886 × 514 |
| `FluidEditor-NoSolver.png` | the window with no Vulkan device |

Refresh times in `Results/FluidEditor.tsv` (a median of 0.9 s at 48³ and 3 s at 96³) are SwiftShader on a CPU in a Debug build, not figures for a GPU.

## Reproduce

```sh
export PATH=$HOME/.local/bin:$PATH
cmake -S . -B _ClaudeScratch/build/UI -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DFRONTIER_HOST_VERIFICATION=ON -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF
ninja -C _ClaudeScratch/build/UI PyroVolumeValidation FluidEditorCapture FluidPlumeEditorValidation
SS=ExternalPackages/VulkanCompute/SwiftShader
FRONTIER_VULKAN_LIB=$SS/libvulkan.so.1 VK_ICD_FILENAMES=$SS/vk_swiftshader_icd.json LD_LIBRARY_PATH=$SS _ClaudeScratch/build/UI/PyroVolumeValidation --require-vulkan
python3 Automation/GenerateGasPresets.py --check && python3 Automation/GenerateGasSceneCodec.py --check && python3 Automation/GenerateFluidEditorControls.py --check
Exhibits/FluidEditor/Code/RunFluidEditorCapture.sh _ClaudeScratch/build/UI     # minutes on SwiftShader; copies Images/ and Results/ only if every claim held
Exhibits/FluidEditor/Code/MutateGasCatalogue.sh _ClaudeScratch/build/UI
Exhibits/FluidEditor/Code/MutateCamera.sh _ClaudeScratch/build/UI
Exhibits/FluidPlumeEditor/Code/VerifyWindowsRegistration.sh _ClaudeScratch/build/UI
```

Prerequisites: the dependencies of `Automation/Prepare*.py`, `node` 22 (the controls generator and the two reference dumps read the prototype's modules), a Vulkan
loader (SwiftShader here). Tested on `e19671c` (GCC 12.2, Debug); the pictures and results are from that revision, and the commit that adds this README adds only documents, scripts, pictures and results.

## Not done, and not checked

Failed, not run and open are listed apart.

**Failed**

- 🔴 The MSVC build of run `42210475527` failed. Its first run (`42202642917`, `d3c48f9`) stopped on two faults that are in `main`: `ProjectContent.h` took the
  C++17 branch under MSVC, because MSVC leaves `__cplusplus` at 199711L (fixed: the guard asks for `__cpp_lib_char8_t`; that error did not come back), and an
  internal compiler error (C1001) in `UI/SolidArc/Document/SolidArcDocument.cpp` at `CombineSolidArcBodies`. The change made for the second (a switch for the
  three-way conditional, a plain `std::set` and an insert) did not remove it: the next run reports the same C1001, now at the first `case` of the new switch.
  The cause is not known and MSVC cannot be run here. The Zig-built `Frontier.exe` is what the pre-release carries; no MSVC-built package exists.
- 🔴 `SolidArcDocumentValidation` (run here only to check that change, Debug, sharing the CPU with the capture) ended with two red gates and all others green: W72's
  speed limit (129 ms a library cut against 50 ms) and W94's (9.96 s and 18.9 s against 1.5 s). They are timing gates of geometry that the change does not
  touch, and the program was not run on `main` to compare.

**Not run**

- The Fluid editor on Windows, on a GPU or in a browser. CI started the package on Windows with a software driver and stopped at the Project Browser and the
  Scene Viewport; it does not open the Fluid editor, so the window and its solver ran on Linux under SwiftShader only.
- The solver's GLSL 450 programs were not compiled by `GenerateVulkanShaders.py` (it needs `glslang`, not available here), so `VulkanSpirvLibrary.h` is unchecked
  against its generator, and their parity with `shaders-glsl.js` was not tested pixel by pixel.

**Open**

- The deviations from the prototype listed above (time step, surge, bounds box, shrapnel, shockwave shell, floor grid and voxel lines, `#object-name`).
- The solver and its context are the other agent's, on a Vulkan context of their own, not Slang programs on `DeviceExchange` as the rest of the engine's are; the
  naming of the imported files was not reviewed against `SKILL-Naming.md`; `PyroVolumeSolver` still carries `WorldSdf*` members and `RecordCompositeIntoWorldTarget`
  that nothing here calls.
- A Fluid volume is drawn in its editor's viewport only. It is not drawn into the Scene viewport's own render (no car, no ReSTIR lighting, no scene shadows), as the
  Smoke & fire plume is not.
- The vehicle's world-grid smoke is the plume of `Exhibits/PlumeSimulation`, not this editor; its checks are that exhibit's.
