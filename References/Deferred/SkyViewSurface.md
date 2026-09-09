# The sky-view surface we do not have

Written after reading the reference project's `.slang` atmosphere shaders at
`Engine/SlateCompute/Compute/AtmosphereIntegrator/Shader/` and
`Engine/Shared/AtmosphereProjection.slang.h`.

## What they do

Three surfaces, and the division of labour is the point:

| | surface | parameterisation |
|---|---|---|
| ① | Transmittance 256x64 | **linear** in cosine, full range, `ZenithCosine = 2*CoordinateX - 1` |
| ② | Multi-scatter 32x32 | *the same mapping as ①*, shared deliberately |
| ③ | **Sky-view** | **quadratic about the horizon**, "below the halfway coordinate the ray descends and above it climbs" |

Two things they do that we do not:

1. **The transmittance path is not cut at the ground.** Their note: a ray that descends
   through the planet "accumulates the whole of the dense lower atmosphere twice and
   extinguishes to nothing on its own ... it writes it continuously rather than as a step
   the horizon texels straddle, which is where the surface's own filtering would otherwise
   smear a discontinuity." That is a cleaner answer to the horizon discontinuity than
   restricting the mapping, and it lets ① and ② share one table.

2. **They have a sky-view surface and we do not.** All their horizon detail lives in ③.
   Ours has nowhere to live, which is why we kept trying to squeeze it out of ①.

## What we measured before adopting anything

Transmittance reconstruction error above the horizon, bilinear vs exact integration, over
altitudes 0 / 500 / 4000 m:

| mapping | 256x64 | 64x32 | 32x32 |
|---|---|---|---|
| signed-sqrt cosine, ground-clamped (was shipping) | 2.297x | 2.796x | 7.273x |
| **Bruneton distance ratio (shipping now)** | **1.320x** | **1.702x** | **2.215x** |
| reference: linear cosine, path not cut at ground | 2.895x | 17.756x | 68.222x |

So we keep Bruneton for ①. The reference's linear mapping only holds up because their
table is 256 wide, and even there it is more than twice our error; it collapses at smaller
sizes. Their design buys simplicity and one continuous table serving every angle, and pays
for it in texels.

⚠️ Honest limit of that table: it measures ABOVE the horizon only. Their unclamped table
also answers below-horizon queries continuously, which ours cannot — we need the separate
multi-scatter mapping for that. Both designs work; they trade differently.

## The actual gap

**③ is missing.** The reference concentrates horizon detail in a sky-view surface,
quadratic about the horizon, which is also what Hillaire 2020 §5.3 prescribes:
`v = 0.5 + 0.5*sign(l)*sqrt(|l|/(pi/2))` with `l` the latitude in ANGLE space, not cosine.
Hillaire's stated reason is that a linear latitude parameterisation "reaches the artist as
a banded sunset over a perfectly smooth zenith".

This is the most likely structural reason our sky does not look like theirs, and it is not
something more accuracy in ① can fix. Not started.
