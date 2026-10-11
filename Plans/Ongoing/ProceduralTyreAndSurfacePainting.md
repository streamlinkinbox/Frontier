# Procedural tyre, lattice deformation, editor tooling and surface painting

Four phases. Phase 1 is a port with a measurable gate, Phase 2 is the piece that makes the generated
tyre deform, Phase 3 is editor surface, Phase 4 opens the painting application. Rims are explicitly on
hold; the rim generator on `arena/01a0f767-slate` is not part of this plan.

## Decision: cage deformation, not vertex animation textures

VAT was proposed because the generated tyre is high poly. It is the wrong mechanism here and the reason
is not performance, it is that a VAT replays a **fixed** animation indexed by `(vertex, frame)`. XPBD
deformation is not an animation; it is a response to vertical load, camber, slip angle, inflation
pressure and the road surface underneath. There is no frame axis to bake against, and the space of
states is not enumerable.

The budget argues the same way:

| Approach | Cost for one tyre |
|---|---|
| VAT, 60 frames, half-float RGB, 100k verts | ~36 MB per canned clip, one clip per load/pressure/camber combination |
| Cage bind data | ~1.6 MB once, shared by all four wheels |

The cage already exists. `XPBDSoftTyre` runs a lattice of `RingCount × SegmentCount` = 9 × 128 = 1152
nodes. That is the simulation mesh; the generated tyre is the render mesh; binding one to the other is
the ordinary arrangement and it is exact at the node positions. It also satisfies the shared-topology
rule directly: one topology, one index buffer and one set of bind weights shared across four wheels,
with only 1152 node positions (about 14 KB) differing per wheel.

VAT keeps one narrow use: a canned loop for parked or distant traffic that is not simulated at all. It
is worth having eventually. It is not the main path.

## Phase 1 — Port the generator, watertight by construction

`References/TyreGenerator.html` is a browser prototype. Its tread mesh is not closed, and
`Exhibits/Workbench/Tyre/TreadMeshAudit.mjs` now measures exactly how badly on the off-road preset, the
busiest tread it ships:

| | before | after the two fixes already landed |
|---|---|---|
| interior cracks | ~80,000 | 13,167 |
| non-manifold edges | 686 | 106 |
| triangles | 291,193 | 67,665 |

The remaining 13,167 are structural and will not be patched out of the prototype. The builder emits every
floor piece and every groove wall as an independent triangle soup and relies on separately triangulated
polygons agreeing on their shared vertices. The port closes them by construction with a single shared
vertex pool and an explicit index buffer, which is a change to how the builder is organised rather than
another patch to it.

### Files

```
Frontier/Engine/Generators/Tyre/
    TreadSpecification.h            carcass parameters: width, aspect, rim, depth, crown, shoulder, wear
    TreadPatternSpecification.h     the layer sequence: circ, lateral, chevron, sipe, dimple, hex, noise
    TreadRegionSolver.{h,cpp}       boolean stack to depth-ordered floor pieces
    TreadMeshSolver.{h,cpp}         pieces to one indexed watertight mesh
    TyreProfileSpecification.h      crown, shoulder arc, sidewall bezier, bead seat
    SidewallMeshSolver.{h,cpp}      the lathe, and its seam to the tread shoulder
    TyreMeshStructure.h             positions, normals, uvs, tangents, indices, material groups
    TreadPatternCodec.{h,cpp}       specification to and from the project format
```

### Dependency

Clipper2 (C++, Boost Software Licence) through `python Tools/Bootstrap.py --package clipper2`. Per
`CLAUDE.md` a build change moves together: the MSVC project, `Module.toml` and the orchestration scripts,
and any g++ helper paths used by the workbench harnesses.

### Gate

`Exhibits/Workbench/Tyre/TreadMeshProof.cpp` — the audit above, in C++, against `TreadMeshSolver`.
Acceptance is **interior cracks 0, non-manifold 0, degenerate 0**, every UV chart injective, and the
shoulder seam between tread and sidewall closed. The `.mjs` audit stays as the prototype's gate.

## Phase 2 — Lattice skinning

```
Frontier/Engine/PhysicalDynamics/Vehicle/LatticeSkinIndex.{h,cpp}
Frontier/Engine/Shaders/LatticeSkinning.slang
```

Each render vertex stores the lattice cell that owns it, bilinear weights inside that cell, and a signed
offset along the rest normal. Built once against the rest lattice and the rest mesh, so it is part of the
shared topology and not per wheel. At runtime a compute pass reads the 1152 deformed node positions and
writes deformed positions and normals. Normals are rebuilt from the interpolated cell frame rather than
from finite differences, because finite differences across a groove wall produce garbage at exactly the
places the tread has the most geometry.

### Gate

`Exhibits/Workbench/Tyre/LatticeSkinProof.cpp`

1. At the 1152 node positions the skinned result is exact against the lattice, to float tolerance.
2. The mesh stays watertight under deformation — the Phase 1 audit, re-run on deformed positions.
3. No self-intersection at 100% sidewall travel, which is the load the ramp landing already reaches.
4. A `ProjectDriveGeneratedTyre` sequence in the Drive gallery, rendered through
   `RunDriveMirror.py --only ProjectDriveGeneratedTyre` so it is a seven minute loop rather than thirty.

## Phase 3 — Editor, prototyped in HTML first

The editor surface is prototyped as HTML before any C++ is written. There is precedent for this in the
codebase and it is the reason this plan can be specific at all: `References/TyreGenerator.html` and
`References/PaintingSurface/` are both working prototypes that settled their models before anything was
ported.

```
References/TyreEditor.html          outliner rows, the asset window, the inspector strip
```

The prototype answers the layout questions — what belongs in the always-present inspector versus the
separate asset window, how the layer sequence reads, which values want sliders and which want numeric entry —
and only then does the C++ get written against the answer.

### Then, against the SolidArc template

`Frontier/Editor/AuthoringTools/Modelling/SolidArc/` is an existing procedural tool with a document, an
undo sequence, a standalone editor host and an outliner adapter. The tyre tool mirrors it rather than
inventing a second arrangement.

```
Frontier/Editor/AuthoringTools/Modelling/TyreForge/
    Document/TreadSpecification.{h,cpp}        the document is the specification
    Document/UndoSequence.{h,cpp}              SolidArc's pattern
    Editor/TyreForgeEditorHost.{h,cpp}         the separate asset window
    Editor/TyreForgeOutlinerAdapter.{h,cpp}    rows into the shared OutlinerPanel
Frontier/Engine/Editor/TyreInspectorPanel.{h,cpp}
    void RecordTyreInspector(ControlPanel&, EditorInstance&, EditorSheet&);
```

That signature matches `RecordCameraInspector` exactly, so the quick sliders land in the existing
inspector while the full editor is a separate window — the split the brief asked for.

Outliner rows: **Tyre** with children **Carcass**, **Tread pattern** (its layer sequence), **Sidewall decals**
and **XPBD lattice**, using role bits in the shape of `SolidArcOutlinerFilter`.

### Inflation pressure

There is no pressure in the tyre model today. `XPBDSoftTyre::Parameters` authors carcass stiffness as raw
compliances, which is not a quantity anybody can reason about from a slider. The plan adds
`InflationPressure` in kPa as the authored value and derives the hoop and spoke compliance from it, so the
slider means something physical and the compliances become an advanced override rather than the interface.

Exposed in the inspector: inflation pressure, tread depth and wear, rim bottoming with its clearance and
damping ratio, solver iterations, ring and segment counts, and the compliances behind an advanced toggle.

### Gate

`Exhibits/Workbench/Tyre/TyrePressureProof.cpp` — sweeping pressure down must lengthen the contact patch
and lower the vertical rate, both monotonically. The harness already measures contact node count and Fz,
so the measurement exists; what is new is the parameter it is swept against.

## Phase 4 — Surface painting

**This phase does not get designed here. It already is designed.** An earlier version of this plan
proposed baking the painted result to an atlas as the source of truth, with decals and strokes
composited into it. That is wrong, and the correction matters enough to state plainly: the authored
thing is the *stroke*, not the texels it happens to light up, and texels are a derived, revisable
residency decision.

The architecture lives in a numbered specification sequence in `SultanAladin/Slate`, vendored here as
`References/SlateConstructionSequence/` and **adapted for this tree** — Slate CAD builds `Slate*.lib` units
behind a `Layer1`–`Layer5` split, which this flat engine does not have, so the thirteen painting documents now
carry a Frontier subsystem in their position table and keep the original unit as `Origin`. The subsystem map is
`Docs/SurfacePaintingArchitecture.md`. The painting story spans these documents:

| Doc | What it fixes |
|---|---|
| `20-SurfaceTileSpace` | Painting is resolution-independent. A stroke is recorded against the surface's parametric domain, not a pixel population. Residency is demand-driven, promotion budget-bounded, tiles 128 texels per edge with a 4-texel border. |
| `22-ImpressionSequence` | A stroke is an ordered sequence of resolved brush impressions in the domain, committed as a transaction and undone by its inverse. A stroke therefore survives a change of working resolution. |
| `24-UvSurfaceDepot` | The UV surfaces themselves. |
| `56-SurfaceLayerSequence` | The ordered content of a surface: painted, placed, and analytically resolved, and the order it is read in. |
| `58-BrushSpecification` | What a brush is. |
| `68-ChartPartition` | Chart layout. |
| `70-AnalyticProjection` | Content that is a description rather than texels — outlines, patterns, placed sources. |
| `72-DecalProjection` | Placed text, imagery and vector outlines that stay editable. |
| `54-TilingSpecification` | Repeating pattern as a declaration of plane symmetry. |
| `10-DocumentStructure` §2.3–2.4 | `RevisionSequence`, scrubbable in both directions; every transaction invertible. |
| `84-RevisionPanel` | Scrubbing the history: backward replays inverses, forward replays operations. |
| `50-AssetInterchange` | Where baked channels go when they leave for other programs. |

Three consequences for this work.

**The history is the asset.** Strokes, decals and placed content are stored as structure, so the paint
history can be rewritten and the same surface re-resolved at a different resolution without repainting.
Baking is an *export* path under `50-AssetInterchange`, not the representation.

**Evaluation is device-side.** `20` and `22`'s resolution half both sit in `SlateCompute.lib` /
`Layer4_Compute`. Texels are produced on the GPU against a demand-driven, budget-bounded tile residency,
which is what keeps a repaint off the CPU rather than spiking it.

**The names already exist, and so do the bans.** The earlier draft of this plan invented
`PaintLayerDepot`, `LayerCompositeSolver`, `SurfaceBakeSolver` and `TyreChartProjection`. All four are
withdrawn — the sequence already names these things. `Stack` is banned (`56`): the ordered content of a
surface is a `SurfaceLayerSequence` and a position in it is a sequence position. `Stamp` is banned
(`22`): one resolved brush placement is an `ImpressionSample`. `Mip` and `Table` are banned too.

### What this phase actually delivers

Not a new subsystem — the tyre as the **first surface** to go through the existing one:

1. Tyre charts produced through `68-ChartPartition` and held in `24-UvSurfaceDepot`, rather than a
   bespoke tyre-only projection. The generated tread already emits `u = x/circ`, `v = (y+A)/(2A)`;
   that becomes a chart contribution, not a private UV convention.
2. The sidewall decal work on `arena/01a0f767-slate` (commit `38440b0`) re-expressed as
   `72-DecalProjection` placements, so lettering stays editable instead of being burnt in.
3. The tread pattern itself is a candidate for `70-AnalyticProjection` — it is a description, not
   texels, which is exactly what that document is for.
4. The `References/PaintingSurface/` prototype remains useful as the **interface** record — its layer
   kinds, its channel order, and its insistence that paintability is enforced in the stroke path. It is
   not the architecture; `20`, `22` and `56` are.

### Gate

Re-resolve a painted tyre at two working resolutions from the same `ImpressionSequence` and compare:
the result must agree within filtering tolerance, which is the property the whole design exists to buy.
Then scrub the `RevisionSequence` backward and forward across a paint session and assert the surface
returns to the same state.

## Order, and why

Phase 1 first, because Phase 2 cannot be gated until the mesh is actually closed — a watertightness
assertion on a deformed mesh is meaningless if the rest mesh already has thirteen thousand open edges.
Phase 3 can start its HTML prototype in parallel with Phase 1 since it shares no code. Phase 4 depends on Phase 1 for charts, and on the construction
sequence being implemented — the tyre is its first surface, not its driver. Implementation order within Phase 4
follows the sequence's own numbering: `20` (the domain and its residency) before `22` (strokes against it),
because a stroke needs a domain to be recorded against.

## Open questions

1. **Naming — resolved.** The construction sequence's names were checked against the closed suffix list in
   `SKILL-Naming` and are already compliant: `Sequence` is defined as "ordered workflow — strokes, layer
   order, revisions", `Space` as "spatial subdivision — tile", `Depot` as "a store of derived, evictable,
   reconstructible artefacts". So `ImpressionSequence`, `SurfaceTileSpace` and `UvSurfaceDepot` need no
   renaming. Only the neighbouring SolidArc tool is off-list (`FigureRecipe`, `SceneDocument`,
   `OutlinerAdapter`); it is treated as legacy and not copied.
2. **Triangle budget.** One tread is 67,665 triangles at `polyDetail 6`, so four wheels is roughly 270k
   before the sidewalls. Acceptable for a hero vehicle, needs LODs before traffic. The target wants
   setting before Phase 1 rather than after.
