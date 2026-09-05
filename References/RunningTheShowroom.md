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

The build script mirrors the freshly linked exe, its PDB, `glfw3.dll` and the lowered SPIR-V into `Build\` at the
repository root, so this path is always the binary you just built. If you ran an older revision that lacked that copy
step, delete `Build\` once — a stale exe left there by hand is the usual cause of "I rebuilt but nothing changed".
The canonical output also remains at
`Projects\Project-Zero\Build\Output\Windows\Release\Binary\Project-Zero.exe`.

### Older CPUs — read this before the first build

The scripts default to **`-Isa SSE2`** (baseline x64), which runs on any 64-bit machine. If your CPU supports it you
can opt into faster code:

```powershell
powershell -ExecutionPolicy Bypass -File Projects\Project-Zero\Build\ToolchainSequence.ps1 -Isa AVX2
```

⚠️ **Sandy Bridge Core i3 (e.g. i3-2120) has no AVX at all** — only the i5/i7 parts of that generation do. Building
with `-Isa AVX` there produces an exe that dies instantly with `0xc000001d STATUS_ILLEGAL_INSTRUCTION`. Leave the
default alone unless you know the target supports the wider set.

`-Isa` **must be identical for every Frontier target you build**, because Jolt derives `JPH_USE_AVX`/`SSE4_2`/`SSE4_1`
from the compiler's macros and `RegisterTypes()` aborts at run time when the library and its client disagree.
`Project-Physics` forwards its `-Isa` to `BuildJolt.ps1` automatically; if you switch ISA, pass `-Rebuild` so the
stale `Jolt.lib` is discarded.

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

**The interface panel is NOT rendered in this build.** `--scene showroom` gives you the room it will hang in, and
nothing more — the room is the Cornell box widened and refurnished, so if it looks "like the Cornell box but
called Showroom", that is exactly right.

Concretely: `GameExecution.cpp` never constructs `InterfaceExchange`, never calls `UploadInstances`, and never
calls `RecordInterface`. The shaders compile, the slot layout is proven, the figures are proven headlessly — but
no code path submits them, so the panel cannot appear. That wiring is blocked on a real seam: `SwapchainExchange`
keeps its `VkDevice`, image views and command buffer private, and `InterfaceExchange::Bring/Resize/RecordInterface`
need all three. Exposing them is the next task, not a one-line hookup.

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
