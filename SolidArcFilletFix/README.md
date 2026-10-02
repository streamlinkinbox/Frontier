# SolidArc — fillet cap-arc direction fix (the "pink segment" on `fillet ... --edges=`)

## What the pink/red was

**Neither a flipped fillet face nor a bad normal — it was a hole.** The viewport shader deliberately tints any
back-facing triangle warm red (`SurfaceRaster.slang`: `if (!FrontFacing) Colour = lerp(Colour, (0.85,0.30,0.25), 0.6)`),
so pink always means "you are looking at the inside of a surface". In the exercise you were looking *through a gap* in
the planar end face of the block and seeing the inside of the quarter-cylinder fillet behind it.

The gap is the circular segment between the fillet's arc and its chord. `topology Part` after the fillet step showed
why: the two cap edges of the roll were

```
e9   v8→v5  Freeform deg 3  len 2.8284   ← 2·√2 = a straight chord, should be an arc of π = 3.1416
e12  v6→v9  Freeform deg 3  len 2.8284
```

so the planar end faces were trimmed by a straight diagonal while the fillet surface itself was a true arc.
Volume reported 115.0409 instead of the closed-form 116.5664 (120 − (4 − π)·4).

## Root cause (`Kernel/BlendSolver.cpp`, `BlendSolver::FilletEdge`)

`FilletEdge` builds the body twice — once leaving the chamfer's straight cap edges ("Plain") and once rebuilding
them as arcs on the tangent cylinder ("Arced") — and keeps the one whose volume is closest to the closed form.

The arc rebuild walks the coedges of the *roll face* and calls `ArcThreePoints(S, mid, E)` with `S`/`E` taken in
that coedge's traversal direction. But the curve is stored on the **edge**, whose parameterisation must run
`VertexStart → VertexEnd`. On the box edge the roll face's coedges are reversed (`f6 outer[ e9- e14- e12- e5- ]`), so
the arc was stored backwards. The planar cap's coedge (`e9+`) then traced it end→start, the cap's trimming ring
self-intersected, the ear clipper collapsed it to 4 triangles with one flipped, and the Arced candidate's volume came
out at 110.35 — *below* the chamfer wedge, which the self-check rightly rejects. The solver therefore fell back to the
Plain chord-capped body: a valid-looking manifold whose end faces don't reach the fillet.

## Fix

One line: if the roll-face coedge is reversed, store the reversed arc so the edge curve runs start→end.

```cpp
if (Section && Result.Coedges[Coedge].Reversed) Section.Payload = Section.Payload.Reversed();
```

Patch: `0001-SolidArc-fillet-cap-arc-direction.patch` (applies to
`Editor/AuthoringTools/Modelling/SolidArc/Kernel/BlendSolver.cpp`; verified against `arena/01a0ce66-frontier`'s copy,
and the hunk is identical in the d91cd78 tree the editor patches were cut from).

## Result

| | before | after |
|---|---|---|
| cap edges | `Freeform deg 3 len 2.8284` (chord) | `Arc deg 2 len 3.1416` |
| fillet volume | 115.0409 | **116.5625** (closed form 116.5664) |
| exercise final volume | 112.5408 | 114.0625 |
| cap tessellation | 30 tris, chord boundary | 66 tris, arc boundary, 0 flipped |
| viewport | pink segment at both ends | closed, no back faces visible |

`Fillet_Before.png` / `Fillet_After.png` are the exercise document rendered from the same camera by the stock
software raster. The final exercise render now has no pink anywhere.

## Regression gates re-run (all green)

BlendVerification · MultiEdgeFilletVerification (28) · PartialEdgeFilletVerification (24) ·
TangentChainFilletVerification (28) · OpenChainFilletVerification (30) · ObliquePartialEdgeFilletVerification (19).

## Note on the exercise file

`SolidArcExercise/SourceCAD_3D_Practice_4.arc` needs no change — rerunning it with the fixed kernel produces the
closed part. The journal's recorded volumes (115.04 / 112.54) were symptoms of this bug.

## Follow-up: grid blur, and the up axis

**Grid.** The `Fillet_Before/After.png` renders above are *fresh*, not stale — but they came from the stock
kernel-branch raster, whose `LatticeProjection.slang` is the old *infinite* lattice: it cross-fades two LOD levels,
widens every line by `1/grazing`, and multiplies by radial + horizon fades, which reads as a blur toward the distance.
The previous session had already replaced that with the one-pixel finite grid (`SolidArcViewportClean/0003`); it just
wasn't in the standalone build I used for the first proofs. The `ZUp_*.png` renders are made with that patch applied.

**Up axis is global Z-up, right-handed.** `CameraProjection` is a Z-up orbit camera (`Vec3::UnitZ()` up hint), the
ground lattice is the z = 0 plane with X red / Y green, the gizmo's blue arrow is +Z, and `workplane xy` is the default
sketch plane. Nothing per-object overrides it. What was wrong was the **exercise journal**, which modelled the part
with Y as height (`box 5 4 6`) and faked the corner cut with a tapered loft. `SolidArcExercise/SourceCAD_3D_Practice_4_ZUp.arc`
redoes it properly: X width 5, Y depth 6, Z height 4, R2 on the vertical back-right edge, and the corner cut as a
single plane through (2.5,0,4) (0,0,2) (0,3,4) built from a triangle lying in that plane extruded along −normal.
Result: `V12 E18 F8`, cut face is a true `Plane`, volume 114.0625 (closed form 120 − (4−π)·4 − 2.5 = 114.066).
