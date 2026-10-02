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
