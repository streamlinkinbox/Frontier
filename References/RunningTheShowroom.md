# Running the showroom on your PC

Everything below assumes a Windows box with a Vulkan-capable GPU. The sandbox this was built in has no GPU, so
the raster path has been proven by SPIR-V compilation and reflection rather than by running — the first real
frame will be on your machine.

## 1. Get the branch

```powershell
cd <your Slate checkout>
git fetch origin
git checkout arena/01a0718d-slate
git pull origin arena/01a0718d-slate
```

If you already had the branch checked out, `git pull` alone is enough.

## 2. Initialise submodules — do not skip this

`ExternalPackages` is empty in a fresh clone, and the build will fail on `toml++/toml.hpp`, `stb_image.h`,
`ufbx.h` or `cgltf.h` if you miss it:

```powershell
git submodule update --init --recursive
```

## 3. Build

```powershell
powershell -ExecutionPolicy Bypass -File Projects\Project-Zero\Build\ToolchainSequence.ps1
```

This compiles the shaders (including the two new `InterfaceRaster` stages) and then the C++. Requires the
Vulkan SDK on `PATH` for `glslc`/`slangc`.

## 4. Run

```powershell
.\Build\Project-Zero.exe --scene showroom
```

On the first run you will see:

```
[Scene] Exported the showroom level to Projects/Project-Zero/Content/Scenes/Showroom.gltf
```

The level is generated from `ShowroomStructure.cpp`, written to disk, and then imported like any other glTF.
Subsequent runs just load the file. Delete `Showroom.gltf` any time you change `ShowroomStructure.cpp` — it is
gitignored and only regenerates when missing:

```powershell
del Projects\Project-Zero\Content\Scenes\Showroom.gltf
```

Other scenes still work as before: `--scene shaderball`, `--scene <any file.gltf|glb>`, or no argument for the
Cornell box. `CornellBox.gltf` is untouched and still the bit-identity reference.

### What you should see

Camera opens at (0, −1.70, 1.45) looking into the room: red wall left, green wall right, a dark plinth centred
with a chrome sphere on it, a matte pillar rear-left, a rough copper sphere rear-right, a deep-blue floor inlay
and an amber strip along the back. Lit by the ceiling luminaire (32 nit) plus a dimmer rear rim strip (9 nit).

**The interface panel is not in this build yet.** P0 proved the panel headlessly; wiring
`InterfaceTrialSequence` into the live frame is the next step. Right now `--scene showroom` gives you the room
it will hang in.

## 5. Re-run the proofs without a GPU

These are what the sandbox uses; they work anywhere with `g++` and clone what they need to `/tmp`:

```bash
bash Scratchpad/CheckShowroomGeometry.sh    # 11 geometry invariants
bash Scratchpad/ExportShowroomLevel.sh      # export → import round trip, luminaires intact
bash Scratchpad/CompileInterfaceShaders.sh  # both raster stages compile, link, and match the C++ slot
```

The interface figure/animation proof (writes PNGs to `Diagnostics/`):

```bash
sed -E 's/\.(xyz|xy|yz|xz|zw)\b([^(])/.\1()\2/g' Engine/Shaders/InterfaceSignedDistance.slang > /tmp/InterfaceSignedDistance.port.inc
g++ -std=c++20 -O2 -I Scratchpad -I Engine -I ExternalPackages/stb \
    Scratchpad/InterfaceRasterTest.cpp \
    Engine/SpatialInterface/InterfaceStructure.cpp Engine/SpatialInterface/InterfaceSequence.cpp \
    Engine/SpatialInterface/InterfaceLayoutCodec.cpp Engine/SpatialInterface/PaletteConfiguration.cpp \
    Engine/DisplayPresentation/MotionIntegrator.cpp \
    Projects/Project-Zero/Source/InterfaceTrialSequence.cpp -o /tmp/InterfaceRasterTest && /tmp/InterfaceRasterTest
```

## If something breaks

| Symptom | Cause |
|---|---|
| `toml++/toml.hpp: No such file` | submodules not initialised — step 2 |
| `glslc` / `slangc` not found | Vulkan SDK not on `PATH` |
| Room renders black | `Showroom.gltf` written by an older build; delete it and re-run |
| Camera inside a wall | stale `Showroom.gltf` from before the camera branch; delete and re-run |
| Shader/C++ slot mismatch after editing a figure field | run `Scratchpad/CompileInterfaceShaders.sh` — the `offsetof` asserts and the std430 reflection will point at the field that moved |
