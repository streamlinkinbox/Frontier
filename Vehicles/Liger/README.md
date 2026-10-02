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
