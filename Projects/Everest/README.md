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
