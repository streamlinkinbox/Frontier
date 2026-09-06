# Interface Light Contribution — making the 3D interface a real light source

**Status:** plan, not yet implemented. Answers "will the interface emit light and show up in reflections?"

## The honest answer for what is built today: no

As of P0-2 the interface does **not** light the room and does **not** appear in reflections. This is not an
oversight in the shading — it follows from where the interface is drawn.

`InterfaceExchange` creates its render pass with:

```
Attachments[0].loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;   // composite over the lit image
Attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
```

That is a raster pass that runs **after** the path tracer has finished and composites on top of the resolved
image. By the time the interface exists, every light-transport decision has already been made. The tracer's
light list is built from geometry:

```
uint LightTriangle(uint lightIdx) { return Luminaires[lightIdx].TriangleSlot; }
```

`Luminaires` holds *triangle slots*. The interface is not in the triangle buffer — it is 14 instanced quads in a
separate storage slot with its own vertex/fragment stages. So it cannot be sampled by NEE, cannot be hit by a
reflection ray, and cannot bounce colour onto the chrome sphere. It is a heads-up overlay that happens to be
positioned in world space.

Two independent consequences, worth separating because they get fixed differently:

| Want | Needs |
|---|---|
| Panel **emits** light onto the room (glow on the plinth, colour bleed) | the panel in the tracer's **light list** |
| Panel **appears in** the chrome sphere / copper | the panel in the tracer's **traceable geometry** |

## Constraint that shapes the whole design

The panel's content is animated at 60 Hz — buttons pulse, a needle springs, digits change. The tracer
accumulates samples over many frames. Anything that changes per frame invalidates accumulation, so a naive
"make the panel real geometry with a real emissive texture" resets convergence every frame and the image never
cleans up. The plan below keeps the *light-transport* description of the panel cheap and slow-changing, while
the *visible* panel stays crisp and fully animated.

## Proposed design: the interface proxy luminaire

### Stage A — emission onto the scene (the glow)

Add a **proxy quad** to the triangle buffer at the panel's exact placement: 2 triangles, one material, appended
last so it lands in the trailing luminaire range the existing convention relies on. It carries a single
**area-averaged emissive colour and luminance** representing the whole panel, not its detail.

The project computes that average — engine stays semantics-free. Every figure the project writes already has a
`Tint`, a `CategoryPalette` and a `HalfExtent`; the average is the extent-weighted mean of tint × emissive
weight over the live figures. One `Vector3` + one scalar per frame.

Crucially this is **updated on a threshold, not every frame**: re-upload the luminaire only when the average
shifts by more than a few percent. A pulsing button moves the panel's *average* almost not at all, so in
practice the light list is static and accumulation survives. The needle sweeping does not change how much light
a 0.4 × 0.25 m panel throws onto a plinth 0.4 m away.

This gets: soft panel glow on the plinth and floor inlay, colour bleed picking up the panel's dominant hue,
correct falloff and shadowing — all through the existing ReSTIR DI path with no shader changes beyond the
material. Cost: 2 triangles, 1 luminaire entry.

### Stage B — appearing in reflections (the chrome sphere)

The proxy quad from Stage A is already traceable geometry, so it will *already* show in the chrome sphere — but
as a flat averaged colour, a glowing rectangle rather than a readable panel. Whether that is enough is a real
question and I would rather you decide it than have me guess:

- **B1 — flat proxy (free, ships with Stage A).** The sphere reflects a soft glowing rectangle in the right
  place with the right colour. Reads correctly at a glance; will not survive close inspection.
- **B2 — SDF-evaluated proxy.** Give the proxy material a hit shader that re-evaluates the same
  `InterfaceSignedDistance` functions the raster path uses, reading the same figure slot storage. Reflections
  become *actually the panel* — buttons and digits legible in the chrome. This is the honest version and it
  reuses code already written and already proven to compile. Cost: the figure storage must be visible to the
  tracer, and reflection rays do SDF work.
- **B3 — render-to-texture.** Rasterise the panel to an offscreen image, use it as an emissive texture on the
  proxy. Conceptually simplest, but it is per-frame-changing texture content, which is exactly the
  accumulation problem above, plus resolution/filtering issues at grazing angles.

**Recommendation: Stage A + B2**, with B1 as the intermediate that falls out for free along the way.
B2 is the reason `InterfaceSignedDistance.slang` was kept as an includable header of pure functions rather than
being inlined into the fragment stage — it can be included from a hit shader unchanged.

### Albedo

Worth stating separately since you asked. Today the panel is pure emission; it has no albedo, so it does not
receive light and stays equally bright whether the room lights are on or off. The `InterfaceInstanceFigure`
slot should carry a **base colour** alongside its emissive tint, so a rounded housing surface can be a dark
grey dielectric that catches the ceiling luminaire and the red wall's bleed, with only the *active* elements —
lit buttons, the meter arc, the seven-segment digits — actually emitting. That is what makes it look like a
physical panel in the room rather than a decal.

This needs a slot field. **The reserved space is already there**: the P0 slot deliberately reserved deferred
fields so P2/P3 would never force a re-layout, and the same argument applies here. Adding base colour +
emissive weight now, while the layout is young and guarded by `offsetof` static asserts, is much cheaper than
adding it after P1 batching depends on the stride.

## Ordering

This slots naturally after P1 (batcher), because Stage A needs the live figure set to average over and B2 needs
the figure storage the batcher owns. Two adjustments to earlier phases:

1. **Now, in P0/P1:** add `BaseColour` and `EmissiveWeight` to `InterfaceInstanceFigure` while the layout is
   still cheap to change. Do not wire them up yet.
2. **After P1:** implement Stage A + B1 together (one proxy quad, threshold-updated). Then evaluate B2 against
   the actual image.

## Resolved: B1 and B2 are one ladder, not a choice

Decision — **build both, selected by a quality tier**, because they share the entire structure and differ only in
what the proxy's hit shader returns. Retrofitting a tier later is what would be expensive; branching one shader
is not.

```
InterfaceFidelityTier            proxy in light list   reflection shows      cost
    Off          .............. no                     nothing               zero — overlay only, today's behaviour
    Low          .............. yes, averaged           flat glowing rect     2 triangles, 1 luminaire
    High         .............. yes, averaged           SDF-evaluated panel   + SDF work on reflection rays
    Ultra        .............. yes, per-figure         SDF-evaluated panel   + one luminaire per emissive figure
```

The tier is one enum on the project side. What changes across it:

- **Off → Low** adds the proxy quad to the triangle buffer and one entry to `Luminaires`. Nothing else moves.
- **Low → High** swaps the proxy's hit shader from "return the averaged emissive" to "evaluate
  `InterfaceSignedDistance` at the hit's plane coordinate". Same geometry, same luminaire, same light list —
  only the returned radiance differs. This is why `InterfaceSignedDistance.slang` stays a header of pure
  functions: the hit shader includes it unchanged.
- **High → Ultra** stops averaging the panel into one emitter and gives each emissive figure its own luminaire
  entry, so a single lit button casts its own correctly-shaped pool of light instead of contributing to a panel
  average. Only sensible for a handful of figures, hence the top tier.

`Low` is the default. It is the tier where the panel visibly lights the plinth and appears in the chrome sphere,
which is the bulk of the perceptual win, at a cost that does not move the frame time.

Note that `Ultra` is the only tier that changes the *shape* of the light list, and it does so by adding entries
rather than by changing their format — so the tiers are runtime-switchable without a rebuild.

## Colour is the only channel, deliberately

Confirmed scope: the panel is an emissive/albedo colour surface — a phone or LCD display — and needs no
roughness, normal or metalness channels. The slot reflects that: `BaseColour` + `EmissiveWeight` and nothing
more. The two `Reserve` floats in the 16-byte std430 tail exist precisely so a future channel can be added
without moving a single existing field offset, but nothing reads them today.

## Implemented ahead of the rest of this plan

The slot fields are **already in**, since the layout was cheapest to change before P1 batching depends on the
stride:

- `InterfaceInstanceFigure` grew from 96 B to **112 B** — `BaseColour` at offset 96, `EmissiveWeight` at 100,
  two reserve floats at 104/108. Verified against glslang's std430 reflection, guarded by `offsetof`
  static asserts on both new fields.
- `InterfaceFigure` gained the same two, defaulting to `BaseColour = 0` / `EmissiveWeight = 1.0` — a pure
  emitter, i.e. exactly the old behaviour.
- `InterfaceConstants` gained `AmbientIrradiance` (96 B → 112 B std140), defaulting to 1.0 in
  `InterfaceViewClip`, so the existing look is bit-identical until a caller dims it.
- The fragment stage now mixes `Albedo × AmbientIrradiance` against the emitted tint by `EmissiveWeight`.

An unset `BaseColour` resolves to the figure's own tint rather than to black, so no existing figure turns into a
black rectangle the moment it is lit. The proof harness asserts that fallback, the round trip of both fields,
and that a default figure is still a pure emitter. `SpatialInterface_P0_Tilted.png` is unchanged.

---

## High tier: status after implementation

**The sampler is built and proven; the plumbing is not.** Worth separating, because "High is done" and "High is
reachable" are different claims and only the first is true.

`Shaders/InterfacePanelSample.slang` walks the composed figures at a hit's plane coordinate and returns the
panel's emission there. It is a header of pure functions with no bindings declared, for the same reason
`InterfaceSignedDistance.slang` is: the raster stage and the kernel bind the same data at different indices, so a
header that hard-coded one would be unusable by the other.

Verified (`Scratchpad/CheckPanelSample.sh`):

- Spatial agreement — a red figure on the left samples red on the **left**, blue on the right, green placed high
  samples in the **upper** half. A mirrored UV returns plausible colour everywhere and is otherwise invisible in a
  blurry reflection, so this is the check that matters.
- The gap between figures reads **dark**. That is the whole difference from Low: an averaged proxy is lit
  uniformly, so visible structure is the evidence the reflection carries layout.
- High's per-texel average vs Low's area-weighted average: **(0.1042, 0.0401, 0.1042)** against
  **(0.1042, 0.0400, 0.1042)**. Two unrelated computations agreeing to four decimals.
- Compiles to SPIR-V as the kernel would include it, not merely as C++ through the shim.
- `Diagnostics/SpatialInterface_HighTier_Trial.png` shows the real trial panel — needle, tick ring, seven-segment
  readout, progress bar.

### What is left, and why it was not done in the same step

The kernel cannot currently **read** the figure buffer:

1. It lives in `InterfaceExchange`'s own descriptor set. The kernel's set is full to binding 18, which is the
   variable-count bindless texture array and must remain last, so adding a buffer means extending
   `kComputeBindingCount`, inserting before the texture binding, and renumbering 18 → 19 in the shader, the C++
   layout, and every descriptor write site.
2. The kernel must learn which instance is the panel proxy, to know when to substitute the sampler for the
   material's averaged emission.
3. The figure buffer must be uploaded per frame into a buffer the kernel's set owns.

None of that is difficult; all of it is a descriptor-layout change of the kind that breaks quietly, and its
correctness is only observable **on a GPU** — reflection output cannot be verified in this sandbox. Landing the
sampler separately means the part that can be proven here is proven, and the part that cannot is a small, clearly
bounded change rather than being tangled with untested geometry.

`IsTierAvailable` therefore still returns false for High. It will flip in the same commit that lands the binding.
