# BMW M4 (F82) — 2D CAD sketch → 3D CAD model, built in SolidArc

Proof-of-concept answer to *"can you convert a 2D CAD sketch of a vehicle into a CAD file using
SolidArc?"* — **yes**. A dimensioned 2D blueprint of a modern BMW was traced into a SolidArc
construction journal, replayed through the SolidArc kernel (from
[`unassignedinbox/Slate`](https://github.com/unassignedinbox/Slate), branch `arena/01a0fd48-slate`,
folder `Frontier/Editor/AuthoringTools/Modelling/SolidArc`), and delivered as a native parametric
`.arc` document plus a tessellated `.obj`, with rendered PNG proofs at every stage.

## Input (the "2D CAD sketch")

`source/bmw-m4-side-view-2d-cad-line-drawing-blu-4.jpg` — the-blueprints.com drawing of the
**BMW M4 Coupe F82 (2014)**, scale 1:25, all measurements in mm: side, top, front and rear
elevations with dimensions `L 4671 · overhangs 811/1048 · WB 2812 · H 1383 · W 1870/2014`.
Zoomed crops used for tracing: `source/zoom_side.png`, `source/zoom_top.png`.

## Model units and key dimensions

1 SolidArc unit = 1 m (kernel tolerances are absolute metre-scale). Model is authored with the
rear bumper at x=0, nose at +X, ground at z=0.

| Quantity        | Blueprint | Model (bounds from `topology`) |
|-----------------|-----------|--------------------------------|
| Overall length  | 4.671 m   | 4.67 m                         |
| Wheelbase       | 2.812 m   | 2.812 m (axles x=1.048 / 3.860)|
| Overall height  | 1.383 m   | 1.38 m (roof spline apex)      |
| Overall width   | 1.870 m   | 1.87–1.88 m (plan spline ±0.935)|
| Tyre radius     | ~0.335 m  | 0.335 m (255/35 R19 equivalent)|

## Pipeline (see `BMW-M4-F82.arc`, 100 commands, 0 refusals)

1. **Side elevation trace** — closed interpolating cubic `spline` through 29 points read off the
   blueprint side view on the XZ workplane (`BMW_01_SideTrace`).
2. **Wheel arches** — two `circle`s at the blueprint axle stations, exact 2D
   `boolean subtract` from the silhouette (`BMW_02_Silhouette`).
3. **Beltline split** — 2D `boolean intersect` of the silhouette with two `rect`s: lower body band
   (z < 0.820) and greenhouse band (z > 0.780).
4. **Plan trace** — closed `spline` of the top view on XY, extruded; `boolean intersect` with the
   side band gives the full-width lower body (`BMW_03b_PlanSketch`, `BMW_04_TwoBlocks`).
5. **Glasshouse** — greenhouse band ∩ cabin plan spline (Kamm-tail cut at x=1.10, fastback slope
   from the side silhouette) = closed manifold cabin solid, glass studio.
6. **Running gear** — `cylinder` tyres + chrome rims at ±track/2, axle-exact.
7. **Details** — kidney-grille recesses (box `boolean subtract`), door mirrors, exhaust tips.
8. **Deliverables** — `save` native `.arc` (parametric journal, v1 header) and `export` Wavefront
   `.obj` (+`.mtl`, `materials.toml`), chord 2 mm, welded.

Every solid is verified `closed yes / manifold yes / oriented yes, χ=2` by `topology`, and the
saved `.arc` re-opens (journal replay) with all bodies still manifold.

## Proofs (`proofs/`)

| File | Shows |
|------|-------|
| `BMW_P1_TraceOverlaySide.png` | traced spline + arch circles overlaid in red on the blueprint side view |
| `BMW_P2_TraceOverlayTop.png`  | traced plan spline overlaid on the blueprint top view |
| `BMW_P3_SideVsBlueprint.png`  | blueprint side view vs SolidArc side elevation (same scale) |
| `BMW_P4_TopVsBlueprint.png`   | blueprint top view vs SolidArc top elevation |
| `BMW_01…06`                   | construction stages (trace, silhouette, slabs, blocks, wheels, details) |
| `BMW_07…09`                   | orthographic elevations (side / top / front) |
| `BMW_10_Hero`, `BMW_11_HeroRear` | 1600×1000 shaded hero renders |
| `BMW_A_Construction`, `BMW_B_Features` | 2×2 contact sheets |

## Reproduce

```bash
# build the console (dependency-free, g++ C++20) — Makefile in this session's workspace
make -C /home/user/SolidArcMake -j2
# replay the journal
/home/user/SolidArcBuild/SolidArc --proofs <dir> BMW-M4-F82.arc
```

## Kernel limits hit (and worked around)

SolidArc's bounded boolean routes refuse non-generic input loudly (transactionally). Encountered:
cut planes through spline interpolation knots, tangent width laws (plan bulge vs front-elevation
prism), periodic-face seam corners on intersection curves, and coincident vertical edges
(cabin plan end == profile end). Workarounds: 2D profile algebra for clips, clip heights probed
off-knot (0.820/0.780), width law kept in the plan spline only, plan envelope offset from profile
ends. A third "front-elevation" prism/loft pass was attempted for tumblehome and dropped because
the bounded SSI routes refuse the grazing intersections — noted here for honesty.
