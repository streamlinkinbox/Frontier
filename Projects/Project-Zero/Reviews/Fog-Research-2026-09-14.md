# Fog Research — 2026-09-14

Survey for Project Zero fog: atmospheric (aerial perspective) + local
volumetric fog, implemented in `.slang`, lit strictly by ReSTIR.

## 1. Techniques surveyed

**Froxel grids (Wronski, SIGGRAPH 2014).** "Volumetric fog: Unified, compute
shader based solution to atmospheric scattering" — frustum-aligned voxel grid,
density injection + lighting + front-to-back scattering integration in compute.
The industry baseline (Unreal, Frostbite, Unity sample all follow it).
Density per froxel is arbitrary (height fog, noise, injected particles).
Source: [chapter](https://bartwronski.files.wordpress.com/2014/08/bwronski_volumetric_fog_siggraph2014.pdf),
[Unity sample](https://github.com/Unity-Technologies/VolumetricLighting/blob/master/Assets/VolumetricFog/Shaders/Scatter.compute).

**Closed-form slab integral (Hillaire / Frostbite 2015).** Inside a slab of
constant properties the scattering integral has a closed form —
`Tr = exp(-σt·D)`, `Sint = S·(1-Tr)/σt` — instead of per-slice constants.
"Physically-based & Unified Volumetric Rendering in Frostbite", slide 28.
This is the integration math our marcher uses per step.
Source: [Frostbite](http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/),
clean writeup: [Strand](https://www.mattiasstrand.com/posts/volumetric-fog/).

**Analytic fog volumes + height fog (Crytek 2007).** Box/ellipsoid volumes
ray-traced analytically in the shader (object-space unit box/sphere, results
transformed back), plus a closed-form height-fog integral
(`fogInt *= (1-exp(-t))/t`). Still the cheapest correct local-fog shape for a
CPU/shader path with no 3D textures.
Source: [Crytek D3D tutorial](https://developer.download.nvidia.com/presentations/2007/D3DTutorial_Crytek.pdf).

**Volumetric ReSTIR (Lin, Wyman, Yuksel, SIGGRAPH Asia 2021).** Extends
spatiotemporal reservoir resampling to participating media: one path sample
per pixel, cheap transmittance approximations for resampling weights, exact
evaluation for the chosen sample.
Source: [project](https://graphics.cs.utah.edu/research/projects/volumetric-restir/).
Follow-up: Ghost ReSTIR (2025) improves transmittance consistency via
null-scattering tracking — noted, not needed at our scale.

## 2. Design adopted for Project Zero

PZ has no GPU runtime today (CPU test ground) and the engine already owns a
density grid (`FluidSolver`) + a grid marcher (`AtmosphereVolumetrics.comp`,
`AtmosphereIntegrator`). The design meets both:

- **Atmospheric fog (surfaces):** the proven celestial media math
  (`SkyMediaApply` in `FogSpecification.slang`, G3-gated) applied along each
  camera ray to shaded
  surfaces: `surface·Ta + La`. Exact at PZ scale (meters); the sun-path
  transmittance uses a closed-form Beer–Lambert segment (error < 0.3 % at
  100 m, documented in code).
- **Local fog (volumes):** analytic box/sphere volumes (Crytek-style
  intersection) + exponential height falloff + integer-hash value noise
  (deterministic CPU/GPU, no `sin`), raymarched with the Hillaire closed-form
  slab per step, single scattering with Henyey–Greenstein phase.
- **Lit strictly by ReSTIR:** the marcher takes its light
  (direction + radiance + visibility) from the winning ReSTIR DI reservoir
  sample — Volumetric-ReSTIR-style reuse — plus the sky ambient for the
  diffuse multiple-scattering stand-in. No shadow maps, no light loops.
- **Engine grid path:** `FogDensityQuery` doubles as the `FluidSolver`
  injection source, so the same volumes feed the existing GPU grid marcher
  unchanged.

## 3. Deliberate non-goals

Full froxel grid (needs GPU 3D textures; the analytic marcher is exact and
cheaper at PZ scale), temporal accumulation (single-frame CPU demo),
multiple scattering beyond the ambient stand-in, self-shadowing inside
volumes (visibility is one segment-midpoint occlusion ray).
