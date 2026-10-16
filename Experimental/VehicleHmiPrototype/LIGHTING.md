# Clustered LED and automotive lighting

## Recovery checkpoint

Before implementation, the complete existing prototype/configurator was committed
and pushed to GitHub: **8c375ce**, branch **arena/cabcd69d-frontier**. The subsequent
lighting change is a separate commit so it can be reverted without removing that
checkpoint. No branch switch or merge is required.

## Controls and fixtures

**I** cycles Off → Low → High → Fog; **O** is now the distance-slice debug shortcut.
The **Lights · automotive** outliner entry exposes:

- Whole-system, front-headlight and roof-box switches; scene street/tunnel switch.
- Mode-dependent peak candela controls, common output multiplier, roof output share,
  downward aim and beam-width adjustment.
- Automotive, Point, Spot and imported IES distributions for the roof projector.
  Front headlights retain the selected automotive mode's analytic profile.
- Night/day preview actions and live light/cluster counts.

The roof box is geometry attached to the body: two feet, finned black housing,
polished reflector surrounds and eight emissive LED cells. It follows vehicle pose,
appears in shadows/mirrors/Garage and participates in the vehicle's existing visual
lifecycle. It is not a separate detachable physics body. One aggregate projector
represents its eight cells; this is not a ray-traced optical assembly. Two front
projectors are positioned just ahead of the existing headlight lenses. Source
emission switches with the mode instead of remaining bright while lights are off.

A new pole at **(-42, -8)** is near the starting area. There are 21 street heads and
8 tunnel fittings. Street heads have opaque tops/sides, metal reflectors and 12
warm LED boards facing down. Tunnel fittings have shielded cool LED strips. Pole
geometry is included in the distance field for local-light shadowing. Other older
emissive props (hangar strips, signs, gantries, etc.) retain their existing materials;
the street/tunnel toggle does not globally extinguish every emissive prop.

## Profiles and units

- Low: broad foreground, narrower hotspot and softened upper cutoff.
- High: tighter, longer-reaching white distribution.
- Fog: broad, shallow amber distribution with reduced upward spill.
- Spot: inner/outer cone with smooth penumbra; Point: isotropic distribution.
- Street: shielded analytic roadway-like angular distribution.

Built-in profiles are **authored approximations**, not measured IES, optical lens
simulation or road-certified beam patterns. Fog mode shapes the light distribution;
it does not create volumetric fog. Candela controls are converted with the exhibit's
scene-unit scale **0.02**; the sun/exposure/material system is not calibrated as an
absolute-lux measurement tool. Contributions use softened inverse-square falloff
and a smooth range window. No bloom/lens-flare effect has been added.

Local IES import supports LM-63 **Type C, TILT=NONE** with axisymmetric, quadrant,
bilateral and full horizontal tables. Numeric data, angle ordering, type, size and
positive finite peak are validated. Included/external tilt and Types A/B are rejected
with a message. Files stay local in the browser; no upload or external fetch occurs.
The distribution is normalized to its peak, sampled into a **16×16** angular LUT,
and oriented along the roof projector. Runtime peak comes from the mode's candela
control; the file's original peak is shown in the UI. Fine/narrow photometry can
lose detail at this LUT resolution. One imported roof profile is resident at a time;
reload/import is needed after a page refresh. No measured IES file is bundled or
misrepresented as a manufacturer's profile.

## Clustering and GI integration

`ClusteredLights.js` builds a camera-centred world-space **8×8×4** grid (16×16×8 m
cells), with a 32-bit sphere/AABB candidate mask per cell. The fixed pool is **32
local lights**: one roof projector, two front lights, 21 street lights and 8 tunnel
lights when all are enabled. Disabled sources leave the pool. CPU grid construction
is bounded; shaders only iterate candidate bits and then evaluate range/profile.
This is world-space CPU-built clustering, not a GPU-built view-frustum cluster list.

Records and masks append to the shared frame uniform (7,968 bytes total). Byte-wise
copy preserves masks containing bit 31/NaN float patterns. Surfaces outside the grid
fall back to testing the bounded light pool, retaining correctness for distant cards
and mirrors. No new transient particle light slots are consumed.

The main deferred direct pass and card-direct pass evaluate identical photometry,
falloff and distance-field shadows. Card radiosity and final gathering therefore
receive bounced local illumination, rather than a screen-only glow. The forward
mirror pass shares the distributions without local distance-field shadows; the
Garage keeps its independent studio lighting.

Card capture now retains the material family. Card final reads live material albedo
and emission so switching LED sources off also switches their cached emission off.
Mode/material changes invalidate lighting history; moving beams prioritize static
cards in their influence radius without clearing accumulated radiosity every frame.
Additional moving-light priority work is limited to the existing card tile budget,
with progressive refresh. Indirect response may lag; it is not instant path tracing.
Shadows retain the distance field's resolution/soft-shadow approximations, not a
per-light triangle shadow-map atlas. Broad/high-range beams increase GPU card/shadow
cost; hardware performance has not been benchmarked.

## Validation

- `npm test`: **36 tests pass**, including real rim regressions and new IES parsing,
  mask bit 31, light modes/attachment/off states, pool bounds and fixture materials.
- `npm --prefix Tools run test:stability`: all **23** existing scenarios pass.
- `npm --prefix Tools run validate:lights`: **56 frames, 0 errors / 0 warnings**;
  exercises Off/Low/High/Fog, Spot/IES, mirrors, card lighting and zero-source states.
- Earlier effects (36-frame) and plume/HMI (48-frame) integration checks also pass.

Dawn's null backend checks shaders, layouts and submitted commands, not pixels.
A real WebGPU-browser night-driving review of beam shape, source appearance, GI
latency and performance remains required. No rendered visual approval is claimed.
