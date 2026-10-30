# Indirect illumination validation

Headless proof for the Phase 5 indirect half (plan §8.1/§8.2, port order 2):

- `Frontier/Engine/Rendering/DistanceField.*` — analytic SDF union (sphere/box/half-space),
  sphere tracing, segment occlusion, 5-tap AO, cosine-hemisphere indirect irradiance with
  discrepancy-sequence directions, and a baked record grid with trilinear sampling;
- `Frontier/Engine/Rendering/SurfaceCards.*` — per-texel card radiosity (B = E + ρ·F·B)
  with distance-regularised form factors and distance-field visibility between texels;
- the indirect prelude declared in `IlluminationUnits` (Distance\* + Card\* units merging
  into `IndirectRadiance`, read by `DirectRadianceSubmission`).

Oracles are analytic/algebraic: exact SDF values; tracing vs closed-form ray/sphere
(3.02263 = 3.02263); irradiance vs π·L (full hemisphere, exact) and π·L·sin²θ (finite
sphere, 0.4% off); baked-grid error 13.5 → 3.2 mm mean from 16³ → 32³; form-factor
reciprocity as an identity; closed-room sums 0.954 → 0.989 toward 1 under refinement;
texel solver vs an independent patch-aggregated direct solve (max patch error 0.66%);
emitted-vs-absorbed power audit 83% → 90.5% accounted from 6×6 → 12×12 texels (the
regularised kernel leaks discretisation error instead of creating energy — the raw
centre-sampled kernel measured 124% "accounted", i.e. it *created* energy; that failure
is what the symmetric distance regularisation r² + (Aᵢ+Aⱼ)/π fixed, keeping reciprocity
exact).

Latest result: **60/60 checks passed** (GCC, `-std=c++17 -Wall -Wextra -Werror`).
Optional argument: a path prefix that writes `<prefix>distance-room.ppm` — a 128×128
sphere-traced room lit *only* by the distance-field indirect term × AO (PNG master in
`Capture/`; grain is the honest 48-sample Monte Carlo).

Scope honesty: demo WGSL internals were never read — these are the canonical mechanisms
the checked-in unit names describe, at luminance/double precision. One bounce in the
field gather; card radiosity carries the multi-bounce. Slang bodies and demo golden
images remain SDK-phase work.
