# 🧩 Interface Phase P0 — Spatial interface: procedural figures in world space (Project-Zero trial)

First phase of the 3D HMI front end. Establishes the four architecture layers with the smallest honest deliverable:
a **retained figure graph**, a **single instanced draw**, **procedural signed-distance shapes**, and **spring-driven
values** through the existing `MotionIntegrator`. No ImGui, no runtime SVG, no per-figure render target — ever.

## 0. Scope line

**In:** `Engine/SpatialInterface/` (generic shapes, transforms, sorting, instancing), the SDF shader set,
`InterfaceExchange` (Vulkan graphics state + one draw), an extended Project-Zero level (`Showroom`), a small animated
trial interface (`InterfaceTrialSequence`), and a headless PNG + numeric proof.

**Out (deferred by phase):** raycast input and dragging (P2), scissor / nested clipping (P2), screen director and
transitions (P3), offline SVG conversion (P4), lobby flow and the full cluster (P5). Signed-distance glyph text is P1 —
P0 draws digits from the same procedural seven-segment cell so no typography work leaks in early.

**Untouched:** the ReSTIR kernel, traversal, the material stack, and the Control Centre. The figure draw is additive:
with zero figures the frame is byte-identical to today.

## 1. Engine ⇄ project seam (CLAUDE.md §6)

> The engine knows shapes. It must not know what a tachometer is.

| | `Engine/SpatialInterface/` | `Projects/Project-Zero/Source/` |
|---|---|---|
| Knows | rounded rectangles, arcs, tick rings, needles, seven-segment cells, transforms, sorting, instancing, palettes | which parts to place, where, and what values feed them |
| Ships | generic categories + one draw | one trial composition + a test value curve |
| Reuse | 100 % — the real project links these files unchanged | 0 % — deliberately disposable |
| May include | nothing from `Projects/` | anything from `Engine/` |

Consequence: the engine has no `GaugeCriteria`. It exposes an `Arc` with `Start`, `Sweep`, `Thickness`, `Fill` — pure
geometry. "Redline at 6 800 of 9 000 rpm" is arithmetic the **project** performs before writing `Fill`.

## 2. Vocabulary

`Layout` stays a **verb phase** (`ConstructTrialLayout`), exactly as `ConstructControlLayout` /
`ConstructTelemetryLayout` already read in the 2D overlay. It is never the name of a drawable.

| Concern | Name |
|---|---|
| One drawable | `InterfaceFigure` |
| Its kind | `InterfaceCategory` — `Surface`, `Arc`, `TickRing`, `Needle`, `SegmentCell`, `Lamp` |
| Retained graph | `InterfaceStructure` |
| Per-frame walk → instances | `InterfaceSequence` |
| Figure ⇄ GPU slot | `InterfaceLayoutCodec` |
| GPU slot (96 B, std430) | `InterfaceInstanceFigure` |
| Limits, topology, sort key | `InterfaceSpecification` |
| Vulkan state + the one draw | `InterfaceExchange` |
| Colour and radius tokens | `PaletteConfiguration` |
| Counters | `InterfaceMetrics` |

Checked against CLAUDE.md §3: no `Node`, `Widget`, `Pass`, `Buffer`, `Pipeline`, `Atlas`, `Bake`, `Registry`, `Handle`,
`Record`, `Skin`, `Tree`, `Region`, or `Item` in any public name.

## 3. Layer map

| Layer | P0 deliverable |
|---|---|
| ① Figure graph (CPU, retained) | `InterfaceStructure` + `InterfaceFigure`, dirty-marked, descent links |
| ② Batched renderer | `InterfaceSequence` → `InterfaceLayoutCodec` → `InterfaceExchange`: one `vkCmdDraw(4, N)` |
| ③ Interaction | reserved — hit extents live in the figure, unread until P2 |
| ④ Value plumbing | project writes normalised scalars; needles are `MotionIntegrator` channels |
| ⑤ Director | not in P0 — every figure already carries `Opacity`, so P3 adds no fields |
| ⑥ Clipping | not in P0 — every figure already carries `ClipExtent` + `ClipRadius`, written wide open |
| ⑦ Ordering | in P0 — CPU sort, opaque first, then transparents back-to-front by view depth |

Fields for ⑤ / ⑥ are reserved **now** so P2 and P3 never re-lay-out the GPU slot.

## 4. The GPU slot (the one contract that must not churn)

`InterfaceInstanceFigure`, std430, **96 bytes**, mirrored byte-for-byte by `Engine/Shaders/InterfaceRecords.slang` and
guarded with `static_assert` — the discipline already used for `TriangleIndex` and `DispatchConfiguration`.

```
vec4  RowX, RowY, RowZ      48 B   world transform rows; translation in .w (UI planes never shear)
vec2  HalfExtent             8 B   [m]
float CornerRadius           4 B   [m]
float Opacity                4 B   [-]   ⑤ reserved, 1.0 in P0
vec4  ClipExtent            16 B   [m]   ⑥ reserved, wide open in P0
uint  Category | Palette     4 B   packed 8 : 24
float ScalarAlpha            4 B   fill fraction / segment code / angle
float ScalarBeta             4 B   thickness / warn threshold / secondary
uint  Tint                   4 B   RGBA8
                          = 96 B
```

200 figures × 96 B = 19 KB per frame into a host-visible ring — cheaper than a single descriptor update. A needle move
is a 4-byte write; nothing is re-tessellated, ever.

**Draw shape:** one 4-vertex triangle strip, no vertex extent at all (`gl_VertexIndex` → corner), instanced. Depth test
on, depth write off, alpha over, sorted back-to-front: correctly occluded by world geometry, correctly self-sorted.

## 5. Level — Cornell extended to `Showroom`

`CornellBox.gltf` is **preserved** as the R0 bit-identity reference for the open GPU verification. `Showroom` is a new
level generated on first run, exported through the same `SceneCodec` path as `ShaderBall.gltf`:

* box widened to 4 × 5 × 3 m, red / green side walls retained so colour bleed still reads;
* added plinth, chrome sphere (roughness 0.08), matte pillar, rough copper stand;
* a deep-blue floor inlay and an amber strip, so the interface's own colours are judged against saturated neighbours;
* second dimmer rear luminaire for rim separation.

Scene switches: `--scene showroom` (new default), `--scene cornell` (unchanged reference), `--scene shaderball`.

## 6. The trial interface (project side)

Six parts, one draw, all animated on a 6-second loop — enough to prove the chain, not a cockpit:

1. rounded housing surface;
2. two buttons pulsing idle → highlight → press (no input yet; P2 owns raycasts);
3. a toggle whose knob slides on a spring;
4. a progress bar filling and emptying;
5. one small arc meter with a spring needle — the canary for the SDF maths;
6. a two-digit seven-segment readout counting with the bar.

Overshoot, settle and clamp are all visible within one loop.

## 7. Proof

Because the build sandbox has no Vulkan SDK, correctness is established the way R4b established it — by compiling the
**shader text itself** as C++:

**A. Headless raster** (`Scratchpad/InterfaceRasterTest.cpp`): includes `InterfaceSignedDistance.slang` under
`FRONTIER_CPU_PORT` via `GlslShim.h`, drives the real `InterfaceStructure` / `InterfaceSequence` / `MotionIntegrator`,
and software-rasterises to `Diagnostics/`:

* `SpatialInterface_P0_Figures.png` — every category, edge-AA legibility;
* `SpatialInterface_P0_Trial_t0..t7.png` — eight frames across the loop;
* `SpatialInterface_P0_ContactSheet.png` — the strip, so needle overshoot-and-settle reads as a sequence;
* `SpatialInterface_P0_Tilted.png` — under perspective, proving resolution independence.

**B. Numeric acceptance**, printed as a `PASS` / `FAIL` table, non-zero exit on failure: slot size exactly 96 B,
encode/decode bit-exact, one draw for the whole trial, needle settles inside 0.6 s ± 5 %, overshoot within 4–12 %,
clamp never exceeded, SDF coverage monotone across an edge.

**GPU acceptance (your machine):** +1 draw call, trial visible in the Showroom, validation silent, frame-time delta
within noise, window resize tracked.

## 8. Risks stated up front

1. `InterfaceExchange` is the only genuinely new Vulkan surface area; it is modelled on `VisibilityExchange` rather
   than inventing a second style.
2. The Vulkan path cannot be compiled in the sandbox. The shape maths is proven by A/B above; the wiring gets its first
   real compile on Windows — expect one round of fixes, stated now rather than as a surprise.
3. Depth interaction with the kernel blit: if the blit discards the depth target, the trial draws unoccluded. Fallback
   is depth-test-off in P0, ordering fixed in P1 — which one it is will be reported, not silently chosen.
