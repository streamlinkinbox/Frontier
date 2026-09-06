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

The **interface panel** hangs above the plinth, tilted toward you, animating on a 6 s loop: two buttons pulsing,
a toggle knob sliding, a needle sweeping its arc, a progress bar filling, a two-digit readout counting.

### What to try

| Action | What should happen |
|---|---|
| **Click the progress bar** | the fill jumps to where you clicked, and the engine note changes pitch with it |
| **Click the toggle** | the knob springs across and latches |
| **Click either button** | the needle steps down / up |
| **Hover anything** | it brightens under the cursor |
| **Press TAB** | the trial panel fades out, a card wipes in — "SLATE / SHOWROOM P4" with a green tick |
| **Look at the chrome sphere** | the panel glows in it, and lights the plinth beneath |

Once you touch a control the scripted loop stands down for good, so the panel stops fighting you. It does not
resume on a timer — a control that starts moving by itself reads as a fault.

### Flags

```powershell
.\Build\Project-Zero.exe --scene showroom          # the panel, lit, interactive
.\Build\Project-Zero.exe --scene drop              # 12 balls fall, with ray-traced shadows that follow them
.\Build\Project-Zero.exe --scene showroom --silent # null audio driver, for a machine with no sound device
.\Build\Project-Zero.exe --scene showroom --animate # scripted instance motion instead of physics
```

### What is deliberately NOT there yet

- **The reflection shows an averaged glow, not the panel's layout.** That is the light-contribution `Low` tier,
  which is the default. The `High` sampler that would show the needle and readout in the chrome sphere is built
  and proven (`Diagnostics/SpatialInterface_HighTier_Trial.png`) but not yet reachable from the ray-tracing
  kernel — see `References/InterfaceLightContribution-Plan.md`, "High tier: status after implementation".
- **No lobby or full cluster.** That is P5, deferred by choice.

## 5. Re-run the proofs without a GPU

One command runs every headless gate — 18 suites covering the interface, the scene, dynamic geometry and the
shaders. Works anywhere with `g++`; each script fetches what it needs.

```bash
bash Scratchpad/CheckEverything.sh
```

It prints one line per suite and exits non-zero if any fail, so it is usable as a pre-commit or CI step. A failing
suite leaves its full output in `/tmp/CheckEverything.<name>.log`.

Individual gates, if you want just one:

```bash
bash Scratchpad/CheckBuildIntegrity.sh        # every build system's source list resolves; submodules declared
bash Scratchpad/CheckPanelPlacement.sh        # the panel is in the room, on the anchor, facing the camera
bash Scratchpad/CheckPointerProjection.sh     # clicks land on the right figure, right way round
bash Scratchpad/CheckTextProjection.sh        # stroke glyphs render; one draw; writes a glyph sheet
bash Scratchpad/CheckScreenSequence.sh        # transitions land exactly; a moving screen is not clickable
bash Scratchpad/CheckVectorCodec.sh           # SVG converts to figures, right way up; writes a preview
bash Scratchpad/CheckLightProjection.sh       # the panel reaches the luminaire table
bash Scratchpad/CheckPanelSample.sh           # High-tier sampler shows layout; writes a reflection preview
bash Scratchpad/CheckInterfaceAudio.sh        # the bar drives the engine note, counted in firing events
bash Scratchpad/CheckDynamicGeometryBudget.sh # per-frame refit stays inside its budget
```

Several write images to `Diagnostics/` — those are worth opening, because a pixel count proves ink exists and not
that it spells anything:

| File | Shows |
|---|---|
| `SpatialInterface_P1_Text.png` | the stroke font, all 43 glyphs |
| `SpatialInterface_P4_Vector.png` | a converted lucide checkmark |
| `SpatialInterface_HighTier_Trial.png` | the trial panel as a High-tier reflection would sample it |

## If something breaks

| Symptom | Cause |
|---|---|
| `toml++/toml.hpp: No such file` | submodules not initialised — step 2 |
| `glslc` / `slangc` not found | Vulkan SDK not on `PATH` |
| Room renders black | `Showroom.gltf` written by an older build; delete it and re-run |
| Camera inside a wall | stale `Showroom.gltf` from before the camera branch; delete and re-run |
| Shader/C++ slot mismatch after editing a figure field | run `Scratchpad/CompileInterfaceShaders.sh` — the `offsetof` asserts and the std430 reflection will point at the field that moved |
| Crash at launch, `0xc000001d` | built with `-Isa AVX` on a CPU without it. Sandy Bridge **i3** has no AVX — rebuild with the default `-Isa SSE2` |
| `JPH::RegisterTypes()` aborts, "Version mismatch" | `Jolt.lib` and the client disagree on `/arch`. Pass the same `-Isa` everywhere and add `-Rebuild` |
| Rebuilt, but the old behaviour persists | a stale `Build\` from before the mirror step. Delete `Build\` once and rebuild |
| No sound, or no audio device | run with `--silent` for the null driver; the scene still renders |
| Panel not visible | it hangs above the plinth — if the camera was moved, press TAB twice to re-present the screen |
| The whole tree, checked in one go | `bash Scratchpad/CheckEverything.sh` |
