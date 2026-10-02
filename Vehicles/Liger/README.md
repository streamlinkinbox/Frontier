# Liger — CAD rebuild journals (SolidArc)

All `.arc` files are SolidArc native documents **and** the operation history: `reset`, then one console
command per line grouped in commented `# STEP n` blocks. Replaying the file rebuilds the model; every
line is an individually undoable operation. Units metres, Z up, +X front, mirrored about Y = 0
(the Blender file's mirror plane at Y ≈ −48 cm has been recentred).

| file | phase | content |
|---|---|---|
| `Liger_Body_Sketch.arc` | 1 | polyline skeleton: 3D creases + boundaries, five silhouettes on the blueprint box |
| `Liger_Body_Curves.arc` | 1b | the same fitted as `spline`/`line` (252 curves, ≤ 0.5 cm from the Blender surface) |
| `Liger_Body_Surface.arc` | 2 | main shell as **5 lofted NURBS sheets** from 48 station sections (`spline` + `loft --sheet`), plus the arch / panel / crease splines as the trim network |

Proof renders: `Liger_Body_Sketch.png`, `Liger_Body_Curves.png`, `Liger_Body_Surface.png`
(rendered from the journals themselves, not from Blender).

Loft segments (station x in cm): Tail −160…−132 · RearArch −132…−68 · Cabin −68…112 · FrontArch 112…205 ·
Nose 205…246. Each section is one open profile sill/arch-lip → shoulder → roof → shoulder → sill, 41 points,
parameterised with the shoulders as landmarks so the loft's U-lines follow the design lines.
Loft surface → mesh at mid-stations: mean 0.79 cm, max 14 cm (front-arch intake pocket).

Source data: `features.json` (feature polylines), `curves.json` (fitted splines + deviations),
`surface.json` (section grid). Regenerate with `Vehicles/tools/{features,fit_curves,loft_shell}.py`.

Next: trim the sheets with the arch/aperture splines, add the cowl + roof-frame parts, and move the
same pipeline onto Quicksilver and Egoist.

## Replayed in SolidArc (real kernel)

`Liger_Body_Surface.arc` opens in the SolidArc console with **0 refusals**: 200 figures, 454 commands; the five
lofts come out as degree 3×3 NURBS sheets (Tail 69×4, RearArch 149×9, Cabin 309×19, FrontArch 181×11, Nose 85×5 poles).
Renders produced by SolidArc itself (`SolidArc/render_views.arc`, 1920×1200, plastic shading):
`SolidArc/Liger_SA_01_Iso_Curves.png` (with the crease/boundary network) … `_06_Front.png`.

Environment: `bash Vehicles/tools/setup_env.sh` builds the console from `SultanAladin/Frontier-` (sparse clone),
the Python venv and headless bpy; then
`~/.solidarc/build/SolidArc --proofs out Vehicles/Liger/SolidArc/render_views.arc`.

Kernel feedback folded back into the generators: splines need > degree points (3-point curves → `--degree=2`),
no coincident consecutive points (shoulder landmark kept ≥ 18 % of the half-profile in from the ends), and
section direction +Y→−Y so the loft normals face outward (SolidArc tints back faces pink).

## Phase 2b — crease-bounded panel lofts (`Liger_Body_Panels.arc`)

The whole-body station lofts were rejected (every panel smeared into one blanket). `tools/panel_loft.py`
instead rebuilds the shell as **38 strip patches per side**: the body is split in X at the ends of the ten
longitudinal creases (sill, door, bonnet edge, fender, bonnet shoulder, rear shoulder/deck/ledges), each
half-section is split where the active creases cross it, and each strip is lofted on its own (`loft --sheet`),
then the −Y side is emitted with reversed point order (the kernel's `mirror` flips orientation → back faces).

* 699 kernel operations, 0 refusals; loft→mesh deviation mean 0.34 cm, worst 6.2 cm (tail end strip).
* Renders: `SolidArc/Liger_Panels_01_Iso.png`, `02_RearQuarter`, `04_Top`, `05_Elev_A` (side), `07_FrontQuarter`
  — via `SolidArc/render_panels.arc`.
* Known defects still to fix: nose strips beyond x≈215 cm twist into ribbons; a few short "Leg" patches on the
  arch tops face inward; patches are not yet sewn into one shell.

## Phase 2c — curve-to-curve lofts (`Liger_Body_CurveLoft.arc`) ← current direction

Per the design brief: loft the **fitted feature curves** against their nearest neighbour, no polygon topology.
`tools/curve_loft.py` holds an explicit pairing table (`PLAN`): 16 strips per side, e.g. rear-ledge-inner ↔ its
mirror (tail centre), ledge → deck → rear shoulder → quarter line → rear-arch rim / sill crease → sill boundary;
cabin-opening rim (crease 062) → bonnet edge → door crease → sill; bonnet edge → fender crease → arch boundary;
bonnet shoulder ↔ mirror → fender rim; tail lip/face from the cross-body tail curves.  Each strip = two sections
resampled on the common x-range, `loft --sheet`; section order chosen so the normal points away from the body axis.
Finding on the way: the shell between the bonnet edges (x 8..185) is the cabin opening — the glass frame is its own part.

* 32 lofts, 0 refusals.  Renders `SolidArc/Liger_CL_01_Iso / 02_RearQuarter / 04_Top / 05_Elev_A / 07_FrontQuarter.png`.
* Open items: strips are ruled between their two curves (faceted look — add the middle crease as a third section
  or a guide where one exists); not yet covered: rear quarter above the arch for x < −122, nose below the bonnet
  shoulder, door skin x > 131; `Cabin_Rim` faces inward; strips not sewn.

## Phase 3 — +Y side lofted curve-to-curve (`Liger_Body_SideR.arc`) ← current

Per review: curves first (kernel renders `SolidArc/Liger_Curves_0*.png`), then loft them. `tools/loft_side.py`
builds the **right (+Y) side only**, 16 strips, each a loft between two neighbouring feature curves on their common
x-range (`PLAN` table).  Silhouettes are never lofted (flat guides only).  Every strip is measured against the
Blender shell; where a plain two-curve loft is more than 4 cm off (the curved door/fender shoulder), one
*construction section* — the strip midline dropped onto the reference surface — is added as a third loft section
and marked as such in the journal.  Section order is chosen so the loft normal agrees with the reference normal.

| strip | curves | mesh dev mean/max cm |
|---|---|---|
| Deck_Step / Ledge / Outer, Rear_Shoulder, Quarter_Top | 076→074→035→061→028→071 | 0.8–1.3 / ≤2.7 |
| Quarter_Panel (rear-window line straight down to the rear-arch opening), Quarter_Sill | 077(+Y)→Edge_005, 071→030 | 2.5 / 9.6, 2.0 / 4.1 |
| Cant_Rail, Door_Upper, Door_Lower, Sill | 063(+Y)→023→064→030→Edge_003 | 1.1–2.5 / ≤6.8 |
| Fender_Panel (bonnet edge + shoulder as one rail, straight down to the front-arch opening) | 023+025→Edge_024 | 3.9 / 15.8 |

Review fix: 092 (rear) and 064/065/026 (front) are the panel **cut lines** running around the arches — they are
drawn but no longer used as loft rails, so each arch is one panel with vertical rulings (rail point above each
arch point) instead of two overlapping sheets with straight cuts.

Renders `SolidArc/Liger_SideR_01_RearQuarter / 02_Side / 03_FrontQuarter / 04_Top.png`.  Not yet: tail-lamp recess
(curves 076–096, 171/172), nose face (004/010/019/031, Edge_017), bonnet centre, mirroring, sewing.
