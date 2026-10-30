# Texture Studio — Windows x64 test build

Extract the complete ZIP to a writable folder and launch **TextureStudio.exe**. Keep `EngineContent` beside it. Requires Windows 10/11 x64 (Universal C Runtime) and a working Vulkan 1.2 graphics driver. No developer tools are needed to launch this build.

## Test the green automotive paint

1. Select **Racing Green** in Material library.
2. Confirm the grooved shader ball, tapered pedestal, lower foot and metallic trim ring are visible.
3. In Inspector, change **Paint depth**, then **Apply**. The GPU preview must change; **Revert** restores the saved material.
4. Scroll down to **Metallic flakes**. Change coverage, particle size (µm, logarithmic), sparkle, reflectivity, repetition, layers and buried tint. Apply after editing. Toggle **Procedural paint** to compare against the plain physical surface.
5. Change the flake palette, then scroll to **Surface detail** and change orange peel. Apply. The clearcoat normal remains independent of the particle-tilted binder normal.
6. **Save** persists the source. Use **Save as preset** / **Duplicate** to keep an alternate material. Invalid source must preserve the last successfully applied preview.
7. Collapse/expand inspector cards, resize/dock the panels, search/filter materials and switch presets after saving/reverting edits. Detached OS windows are off by default.

On first launch, bundled presets are copied without overwriting existing files into `%LOCALAPPDATA%\Frontier\TextureStudio\Materials`. If an older local catalogue already exists, choose **Open Library** and point it at the extracted `EngineContent\MaterialArchives\TextureStudio`, or launch:

```bat
TextureStudio.exe --library "C:\path\to\extracted\EngineContent\MaterialArchives\TextureStudio"
```

Project layout/settings remain in `.projectspace`. This build does not replace either retained painting prototype.

## Scope and test status

- Actual native C++ application, shared ImGui docking/controls, shared Vulkan renderer and one GPU-resident material thumbnail atlas.
- Automotive paint executes object-space cellular particles, front-to-back buried layers, linear-space minification averaging, palette/ramp/single tint, roughness/metallic variation, particle tilt and orange peel.
- General editor icon catalogue is shared. UV/paint/decals belong to Texture; smoke/water belong to Fluid. Workspace-specific entries are unavailable outside their domain.
- Other catalogue families retain their actual identities and physical material coefficients, but their complete HTML procedural shader families are **not yet ported**. Disabled controls identify this boundary. HTML/native lighting and presentation are not certified 1:1.
- A linked Windows x64 executable is not proof of Windows execution. This sandbox verifies CPU/sanitizer and offscreen Vulkan software-driver behaviour; native Windows launch and hardware-driver testing remain your acceptance step. `Package.json` records the exact packaged files and source fingerprints.

Report launch/driver errors, incorrect green paint, missing base/icon rendering, inspector clipping or Apply/Revert/Save problems with your Windows version, GPU/driver, display scale and a screenshot.
