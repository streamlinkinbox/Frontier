# Project Everest — C++ two-level build

The standalone `Everest` executable starts in the C++ Lobby and has a small navigation menu for the Lobby and Proving Grounds. Selecting either route rebuilds the session with the destination's Everest policy and spawns a fresh vehicle for every carried player. This is the C++ session/presentation path; it does not use the browser HMI.

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

## Download test binaries

Pushing to the session branch runs **Everest C++ binaries** in GitHub Actions for Linux, Windows, and macOS. Each workflow run uploads an `Everest-<OS>` artifact containing the executable, the three ControlVehicle facet archives, and a short run guide. Download the artifact from the Actions run's **Artifacts** section and follow its `README.txt`.

## Validation

`ctest` runs the four programs below, from the repository root. Their device halves need a Vulkan 1.1 loader: without
one they say so and skip. To run them on a machine without a GPU, point `FRONTIER_VULKAN_LIB`, `FRONTIER_VULKAN_DRIVER`,
`VK_ICD_FILENAMES` and `LD_LIBRARY_PATH` at a software loader such as SwiftShader, and pass `--require-vulkan`, so that a
skipped device half is a failure.

| Program | What it holds |
| --- | --- |
| `SessionValidation` | The lobby session, the vehicle and its policies. |
| `PyroVolumeValidation` | The 18 presets, the 85 settings and the Fluid editor's 82 controls against `Experimental/Fluid`; the solver. |
| `WorldGridValidation` | The driving course's world grid: its window rule, smoke that stays where it was laid, and the course. |
| `VolumetricAndEditorValidation` | Both editors on their records, and a car that drives 11 m out of its smoke. |

`VolumetricAndEditorValidation --write-proofs <folder>` writes the six pictures kept in `VisualProof/` (a lowered lattice
on a software device: the presets' own 96 to 128 cells take minutes a refresh there). `Validation/Mutation/*.sh <build
folder>` breaks the camera, the gas catalogue or the world grid on purpose, one place at a time, and reports any break the
validation does not stop; the script puts the sources back.
