# Project Everest — C++ two-level build

The standalone `Everest` executable starts in the C++ Lobby and has a small navigation menu for the Lobby and Proving Grounds. Selecting either route rebuilds the session with the destination's Everest policy and spawns a fresh vehicle for every carried player. This is the C++ session/presentation path; it does not use the browser HMI.

`EverestWindow` is the Vulkan editor of the same project. It opens the project's startup level, the **Lobby**: the
studio with one vehicle (no HMI yet). The vehicle is a Vehicle record of the level (the Control vehicle with its four
tyres, in the Outliner under Vehicles), drawn from the same three ControlVehicle archives as the hero of the HTML Lobby
(`Experimental/VehicleHmiPrototype/Lobby.html`), and the view the window opens with is that demo's hero framing of it:
yaw 1.15 rad round from the nose, pitch 0.20 rad, 6.6 m out, 60° vertical field of view, about the car's centre of mass
raised 0.35 m (Alt orbits it, as the demo's orbit camera does). The demo's idle drift and cinematic tour are not part of
the editor's camera, and the studio is this engine's own, not the demo's path-traced cyclorama. `--proving-grounds`
opens the Proving Grounds level instead. Press **F2** for the performance window.

## Run locally

From the repository root:

```sh
cmake -S Frontier -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# Default: Lobby, with vehicle and studio render.
build/Everest --render lobby.ppm

# Navigate to Proving Grounds and render the same vehicle there.
build/Everest --navigate proving-grounds --render proving-grounds.ppm

# Exercise both menu transitions in one session, returning to the default Lobby.
build/Everest --navigate proving-grounds --navigate lobby --render return-lobby.ppm
```

On Windows, use `build/Release/Everest.exe`. Use `--guests N` (0–4) to spawn more cars and `--xray` to apply the C++ vehicle scan effect to the local vehicle. `--archive DIR` overrides the default vehicle facet archives.

## The editor window

```sh
build/EverestWindow                      # the Lobby, one vehicle
build/EverestWindow --proving-grounds    # the Proving Grounds level
build/EverestWindow --optimized --uncapped --perf-window
```

| Flag | What it does |
| --- | --- |
| `--proving-grounds` | Opens the Proving Grounds level (its course, ramp and obstacles) instead of the Lobby. |
| `--optimized`, `--baseline` | The start state of the optimized-execution switch: off, unless `FRONTIER_OPTIMIZED_EXECUTION=1`. |
| `--uncapped` | Presents without waiting for the display, so that a faster run shows in the refresh period. |
| `--perf-window` | Opens the F2 window at the start. |
| `--perf-test` | Opens it and runs the A/B test once the window is up. |
| `--fluid-editor`, `--smoke-editor`, `--preset ID` | Open the Fluid editor, or the Smoke & fire editor, at the start. |
| `--archive DIR`, `--frames N`, `--capture PATH.png` | The vehicle archive; stop after N refreshes; save the last one. |

Renaming a record in the Outliner is **Shift+F2** (F2 opens the performance window).

## Performance window (F2)

One build holds both ways of running the simulation and the scene. The switch is off until it is turned on: off is the
original path, on is the path that makes the same pictures with fewer waits on the device (the simulation, the solid
atlas upload and the scene share one submission) and less recomputing of the solid atlas (the course's distances are
kept; only the voxels a car can reach are evaluated again). `PerformanceParityValidation` holds that the atlas is the
baseline's bit for bit and that both paths draw the same pictures.

| Part of the window | What it gives |
| --- | --- |
| Optimized execution | The switch. It acts at the start of the next refresh. |
| Live | The last 300 refreshes: the refresh period (mean, 95th and 99th percentile), a plot, and what each part of the work costs. |
| Present without waiting | Turns vsync off; the refresh period cannot go below the display's while it is on. |
| GPU timestamps | The device's own time for the solver, the scene, the smoke composite and the interface, where it has them. |
| A/B test | Runs a scripted drive four times (baseline, optimized, optimized, baseline) at a fixed step with a fixed camera, then reports both paths. |

The A/B test is four runs of about 725 refreshes each, so one to two minutes at 30 to 60 refreshes a second. Leave the window alone while it runs. It reports the mean of each figure over the
runs of each path, how far two runs of one path differ (so that a difference is read against that), and whether the
scene was drawn the same by both paths. **Copy the report** puts the text on the clipboard. Every run is also written as TOML to
`PerformanceLogs/` (next to where the program was started), with the machine, the device, the present mode and the
revision in its header, beside the report as `<time>_ab-report.txt`. Those files are what to send for a second opinion.

How to read it:

- Compare the **refresh period** only when the present mode is not FIFO; with FIFO the display's rate is a ceiling and
  the CPU and GPU rows are the ones that can move.
- A difference smaller than the spread between runs of the same path is not a difference.
- A debug build, or a software device, says nothing about a real one: the report says which it ran on.

## Download test binaries

Pushing to the session branch runs **Everest C++ binaries** in GitHub Actions for Linux, Windows, and macOS. Each workflow run uploads an `Everest-<OS>` artifact containing the executables, the three ControlVehicle facet archives, the project (`Projects/Everest`), the engine content (fonts and icons), and a short run guide. Download the artifact from the Actions run's **Artifacts** section, unzip it, run `EverestWindow` from inside its folder, and follow its `README.txt`.

## Validation

`ctest` runs the six programs below, from the repository root. Their device halves need a Vulkan 1.1 loader: without
one they say so and skip. To run them on a machine without a GPU, point `FRONTIER_VULKAN_LIB`, `FRONTIER_VULKAN_DRIVER`,
`VK_ICD_FILENAMES` and `LD_LIBRARY_PATH` at a software loader such as SwiftShader, and pass `--require-vulkan`, so that a
skipped device half is a failure.

| Program | What it holds |
| --- | --- |
| `SessionValidation` | The lobby session, the vehicle and its policies. |
| `PyroVolumeValidation` | The 18 presets, the 85 settings and the Fluid editor's 82 controls against `Experimental/Fluid`; the solver. |
| `WorldGridValidation` | The driving course's world grid: its window rule, smoke that stays where it was laid, and the course. |
| `VolumetricAndEditorValidation` | Both editors on their records, and a car that drives 11 m out of its smoke. |
| `PerformanceParityValidation` | The log's arithmetic; the optimized atlas against the baseline's, bit for bit; both paths drawing the same pictures. |
| `EverestWindowValidation` | The Lobby start (level, its one vehicle, the view checked against the HTML Lobby's own source, the vehicle on screen) and the F2 window with its A/B test. |

`VolumetricAndEditorValidation --write-proofs <folder>` writes the six pictures kept in `VisualProof/` (a lowered lattice
on a software device: the presets' own 96 to 128 cells take minutes a refresh there). `Validation/Mutation/*.sh <build
folder>` breaks the camera, the gas catalogue or the world grid on purpose, one place at a time, and reports any break the
validation does not stop; the script puts the sources back.

`VisualProof/07_everest_lobby_start.png` is the Lobby as the window opens it: the one vehicle in the HTML Lobby's hero
framing (the scene alone, 640 × 360 on a software device). `EverestWindowValidation --write-pictures <folder>` writes the
whole window the same way, at 960 × 540.
