# Lens Flare: native inspection before the port

**Historical baseline audit.** The first implementation is now documented in [LensFlareNativePanel.md](LensFlareNativePanel.md); the findings below describe the pre-port implementation.

Inspected target: `f6c99702c769ef9fd44c39ac8819e0ebeae35b9b`, reconstructed in `.cache/cpp-sun-full`. This is an audit and implementation plan, **not a completed Lens Flare inspector, bake system or algorithm rewrite**.

## What is actually implemented

The native CelestialSequence sheet exposes **four presets**, not three universally defined flare types:

| Preset | Ghost weight | Halo weight | Streak weight | Burst weight |
|---|---:|---:|---:|---:|
| Cinematic | 1 | 1 | 1 | 0.35 |
| Anamorphic | 0 | 0 | 2.2 | 0 |
| Starburst | 1 | 0.5 | 0.35 | 1 |
| Halo | 0 | 1 | 0.35 | 0 |

These weights come from `Engine/DisplayPresentation/AtmosphericOptics.h::MixFor`. Every preset also includes bloom. They are mixtures of effects, not four separate optical simulators.

The approved HTML `flare-inspector.jsx` instead exposes **three mixable primary effects: Anamorphic, Streaks, Starburst**, plus ghost controls. That matches the user's recollection. Native Anamorphic is a weighted horizontal streak, not an additional independently configurable component. Native Halo should remain available as a supporting component rather than being silently discarded when porting those three effects.

## Current slider coverage

`Projects/Project-Zero/Source/CelestialSequence.cpp` builds and applies:

- Enabled, Type, Intensity (0–3×)
- Ghosts (0–8)
- Halo Radius (0.1–1.2, aspect-corrected screen coordinates)
- Chromatic (0–1)
- Aperture Blades (3–12)

**Halo Radius already changes the native algorithm.** In Anamorphic mode the preset's halo weight is zero, so this slider deliberately produces no visible halo change. The UI must explain this or permit independently enabling/mixing the halo.

**StreakGain exists in settings and the packed renderer record but is missing from this property sheet.** That is a real binding gap.

Missing independent controls include halo gain/width, streak rotation/length/width, independently mixed component gains, and ghost shape/spacing/gain. Current ghosts are circular; HTML round/hexagon/polygon choices do not already have native equivalents.

## Rendering paths that must not diverge

1. **Native interactive property path:** `CelestialSequence::ApplySheet` updates `AtmosphericOptics::LensFlareSettings`; `PackPostRecord` calls `PackPostConstants`. `Engine/DisplayPresentation/PostConstantRecord.h` carries the parameters in the existing 128-byte record.
2. **Viewport shader:** `Engine/Shaders/PostRecords.slang::FlareAlong` mirrors the C++ optics implementation. `Engine/Shaders/ReSTIRViewport.slang` calls it during composition. Shader source inspection is not GPU execution proof.
3. **CPU showcase path:** `Projects/Project-Zero/Source/RendererHost.cpp::ApplyLensFlare` invokes `Projects/Project-Zero/Shaders/FlareSpecification.slang::FlareLensKernel`, compiled as C++. It constructs `FlarePostDefaults()` and changes only variety. Its defaults, coordinate convention, visibility fade and fixed burst formula differ from the interactive path. It does **not** consume all the current CelestialSequence slider values.
4. An additional `Integration/Shaders/FlareSpecification.slang` copy also exists. Its ownership/build consumers need verification when consolidating the implementation.

A new preview must not use the independent HTML canvas drawing as a stand-in for native rendered output. Nor should a fix to only AtmosphericOptics be described as updating every scene-rendering path.

### Starburst issue discovered

The C++ comments state the straight-aperture-blade diffraction parity rule backwards. Ideal even N blades produce N primary rays, odd N blades produce 2N. The current odd-blade angular multiplier also warrants correction: its primary angular expression is not generally periodic over a full turn. The secondary artistic ray term further complicates counting. Correct both C++ and shader together and test angular continuity and primary ray counts; do not simply edit the comment and claim physical accuracy.

## Baked image / preview decision

No flare-specific baked texture lifecycle, import binding or texture sampler was found in the inspected native paths. The flare is evaluated procedurally. The HTML preview can export a canvas image, but this is not a native renderer bake resource.

The required design is **one authoritative generated resource shared by the inspector preview and the bake output**, not two independent drawings:

- Slider changes update one validated settings object and mark the cached output dirty.
- Generate into a linear HDR texture; the inspector samples that texture with a display transform. Preserve additive-light semantics rather than confusing it with straight-alpha colour.
- The bake action exports/freezes that same generated result. Imported/baked mode must explicitly distinguish fixed imagery from editable procedural settings.
- Include settings, algorithm version, resolution, aspect and source position in any full-frame cache key. Camera/source motion must invalidate a full-frame flare image because ghost placement depends on the source-to-optical-axis line.
- A fixed full-frame baked image cannot simply replace a moving camera-dependent flare. A component atlas is an alternative, but it requires the renderer to position/composite those components. Visibility and occlusion remain scene-dependent.
- Share lifecycle/ownership between the tab thumbnail and larger preview; do not regenerate per widget or allocate large image buffers on the stack.

## Proposed implementation order

1. Port the approved Lens Flare composition and existing native sliders. Preserve three primary mixable effect controls and supporting halo/ghost controls; retain useful existing presets as presets, not exclusive component switches.
2. Consolidate settings and renderer parameter transport across the interactive and showcase paths. Expose the missing streak binding and validate all inputs.
3. Implement halo gain/width, independent mixes, rotation and ghost geometry in a shared or parity-tested kernel. Fix burst parity/continuity with explicit tests.
4. Add shared cached texture/preview/bake ownership, invalidation and resource cleanup. Only then enable Bake / Use baked image.
5. Test real pointer input through ApplySheet, packed records, CPU pixels, shader compilation/parity, texture invalidation and low-stack operation. Device rendering and Windows stack verification remain separate gates.

## Executed audit probe

`Exhibits/Workbench/LensFlare/AuditProbe.cpp` samples the **existing** C++ optics implementation at 128×72. All **12 checks pass**: six pairwise preset differences, halo-radius response, streak-gain response, chromatic response, ghost-count response, aperture-blade response and suppressed halo response in Anamorphic mode. Results: `Exhibits/Gallery/LensFlareAudit/Probe.txt`.

Reproduce after preparing the native staging target:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -O2 \
  -I.cache/cpp-sun-full/Engine/DisplayPresentation \
  Exhibits/Workbench/LensFlare/AuditProbe.cpp -o .cache/lens-flare-audit
.cache/lens-flare-audit
```

This proves existing CPU parameter sensitivity, not native bake availability, optical correctness, shader parity or a finished Lens Flare port.
