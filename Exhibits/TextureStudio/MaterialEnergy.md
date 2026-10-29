# Reused material energy diagnostic — historical baseline

**Historical pre-correction results at `b6bc49e`; see [current correction and acceptance](MaterialEnergyAcceptance.md).** The original numeric report below is retained unchanged. Recompiling the diagnostic now uses the corrected evaluator.

**Existing evaluation reused, not rewritten at that checkpoint.** Native C++ calls `Frontier::EvaluateScatteringSequence` from `Engine/Render/ScatteringSequence.h`. All 24 fixtures remain finite, nonnegative and reciprocal; **three exceed the white-furnace threshold**. This diagnostic is not full-library acceptance.

| Check | Result |
|---|---|
| Compiler | GCC 12.2.0, C++20, `-O2 -Wall -Wextra -Werror` |
| Profiles | White dielectric, coloured metal, coated white dielectric, fabric with sheen |
| Coverage | Four profiles × roughness 0.35/0.65 × view cosine 0.25/0.6/1 = 24 fixtures |
| Integration | Hemisphere midpoint quadrature, 256² and 512² directions; integrate `BRDF × cos(light)` |
| Finite / nonnegative / reciprocity | ✔️ All evaluated directions; reciprocal tolerance `1e-10` |
| Coarse/fine sensitivity | Maximum channel difference `0.000931824158` |
| White-furnace reflectance ≤1.02 | 🔴 3/24 excesses; maximum `1.36977433` |
| Production shader/preset changes | None |

## Failing cases

| Profile | Roughness | View cosine | Integrated maximum reflectance |
|---|---:|---:|---:|
| White dielectric | 0.35 | 0.25 | 1.12791841 |
| Fabric, additive sheen | 0.35 | 0.25 | 1.36977433 |
| Fabric, additive sheen | 0.65 | 0.25 | 1.26162138 |

The native scalar isotropic Schlick path is consistent with the inspected shared material shader's diffuse/specular and additive sheen structure. These findings require investigation before claiming energy-conserving reused materials. Reciprocity and numerical convergence do **not** cancel the energy failures.

## Reproduction

```bash
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I. \
  Exhibits/TextureStudio/Code/MaterialEnergyBaseline.cpp \
  -o _ClaudeScratch/build/MaterialEnergyBaseline
_ClaudeScratch/build/MaterialEnergyBaseline \
  > Exhibits/TextureStudio/Results/MaterialEnergyCurrent.txt
```

[Full numeric output](Results/MaterialEnergyBaseline.txt). The executable returns nonzero for invalid/negative/nonreciprocal evaluation. Energy excess is deliberately reported diagnostically, **not** a passing energy assertion; exit zero must not be used as an energy-conservation acceptance gate.

Not covered: every archived preset, anisotropic/spectral/dispersion modes, all authored strata, full native Slang scene lighting, or sampling/PDF consistency. The [upstream identity audit](MaterialReuse.md) remains separate from these optical checks. The [two-render glint comparison](GlintPair.md) uses an isolated GLSL fixture; it does not validate this full native material evaluation inside the vehicle renderer.

The subsequent [energy-allocation correction](MaterialEnergyAcceptance.md) now exercises both native and actual shared Slang evaluation, with **both analytic and emissive lighting** retained in the renderer. The user has selected cellular for automotive paint; Glinty remains optional research. Physical RTX/GTX/Windows measurements remain pending.
