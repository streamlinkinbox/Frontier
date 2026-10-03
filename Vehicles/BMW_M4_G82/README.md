# BMW M4 Competition (G82) — 2D Orthographic Blueprint to SolidArc 3D CAD

This directory demonstrates end-to-end automotive CAD authoring in **SolidArc** starting from a standard 4-view 2D orthographic blueprint of the **2020 BMW M4 Competition Coupé (G82)**:

- **Overall Length**: `4,794 mm` (`X ∈ [0.000, 4.794] m`)
- **Overall Width**: `1,887 mm` (`Y ∈ [-0.9435, +0.9435] m`)
- **Overall Height**: `1,393 mm` (`Z ∈ [0.000, 1.393] m`)
- **Wheelbase**: `2,857 mm` (Front Axle `X = 0.875 m`, Rear Axle `X = 3.732 m`)

## Files

| File | Description |
|---|---|
| `blueprint_bmw_m4_g82_4view.jpg` | Source 4-view orthographic CAD blueprint (`Side`, `Top`, `Front`, `Rear`) with factory dimensions |
| `BMW_M4_G82_2D_Sketch.arc` | Native SolidArc 2D/3D wireframe blueprint & station-section CAD sketch (`84` commands, `0` refusals) |
| `BMW_M4_G82.arc` | Complete SolidArc 3D NURBS + B-Rep vehicle model (`442` commands, `0` refusals, `100%` outward normals) |
| `SolidArc/00_Blueprint_To_SolidArc_Proof.png` | Side-by-side comparison of the 2D BMW M4 G82 blueprint, 2D/3D wireframe sketch, and 3D SolidArc CAD renders |
| `SolidArc/BMW_M4_G82_01..10_*.png` | Multi-angle perspective, orthographic (`Side`, `Top`, `Front`, `Rear`), matcap, and 4-view contact sheet proof renders |
| `SolidArc/BMW_M4_G82_Sketch_01..05_*.png` | Multi-angle wireframe proof renders of `BMW_M4_G82_2D_Sketch.arc` |

## Reproducing from Source

```bash
PYTHONPATH=/tmp/pylib python3 Vehicles/tools/build_bmw_m4_g82.py
/tmp/solidarc-build/SolidArc Vehicles/BMW_M4_G82/SolidArc/render_sketch.arc
/tmp/solidarc-build/SolidArc Vehicles/BMW_M4_G82/SolidArc/render_solid.arc
```
