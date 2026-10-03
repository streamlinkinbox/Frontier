# Liger — Exact Catmull-Clark NURBS Reconstruction in SolidArc

This directory contains the native **SolidArc** (`.arc`) CAD reconstruction of the **Liger** hypercar from `Liger_named.blend`, extracted mesh archives (`mesh/*.npz`), and 3D feature curves (`curves.json`).

---

## Why Previous Reconstruction Attempts Failed (and How It Was Fixed)

1. **Single-Sheet Surface Fitting (`loft_shell.py`)**: Attempted to fit one single $22 \times 18$ tensor-product NURBS sheet over the entire 3D point cloud (`354,464` vertices), producing a wavy rectangular sheet that could not represent wheel arches, undercuts, or multi-panel creases.
2. **Disjoint 1D Curve Lofting (`curve_loft.py`, `panel_loft.py`, `loft_side.py`)**: Attempted to loft across unaligned 3D crease polylines from `curves.json`, creating twisted ribbons, missing panels, and inverted back-faces.
3. **Control-Grid Transposition & `sew --open` Orientation Flip**:
   - In SolidArc's `NurbsSurface::Patch(DegU, DegV, CountU, CountV, Pts)`, `Pole(I, J) = Poles[I * CountV + J]`, so the surface normal is $\vec{N} = \partial_U S \times \partial_V S \propto (P(I+1, J) - P(I, J)) \times (P(I, J+1) - P(I, J))$. Emitting control grids in column-major order inverted $\vec{N}$, causing SolidArc's `SurfaceRaster.slang` shader (`if (!FrontFacing) Colour = lerp(Colour, float3(0.85, 0.30, 0.25), 0.6)`) to render exterior surfaces salmon-pink.
   - Calling `sew ... --open` invokes `BrepBody::Orient()`, which flips all faces if the open shell's signed volume integral relative to the origin happens to be negative.
4. **Exact Per-Face Catmull-Clark B-Spline Patch Reconstruction (`Vehicles/tools/reconstruct_liger.py`)**:
   - Blender's Catmull-Clark `SUBSURF` (`levels=2`) subdivides every base $K$-gon into $4K$ contiguous sub-quads in `T` (`16` sub-quads forming a $5 \times 5$ vertex grid per base quad; $K$ corner $3 \times 3$ grids sharing the face center vertex per base $K$-gon).
   - `reconstruct_liger.py` recovers every $5 \times 5$ and $3 \times 3$ Catmull-Clark limit grid directly from `mesh/*.npz` in pure NumPy and solves the exact clamped bicubic ($4 \times 4$, `1.13 mm` max interior error, `0.00 mm` boundary gap) and biquadratic ($3 \times 3$, `0.00 mm` error) Bernstein control grids $P = A_d Q A_d^T$ with guaranteed outward-facing normals.
   - Non-exterior helper/interior meshes (`Cylinder.003` wheel-well boolean cutter and `Interior_*` cabin guide meshes) are excluded from the exterior assembly, while all 11 exterior body, aero, and wheel/tyre sub-assemblies are reconstructed.

---

## SolidArc CAD Deliverables

| File | Description |
|---|---|
| `Liger_Complete.arc` | Complete Liger hypercar (`Body_Main_Shell` + `Body_Front_Cowl` + `Body_Roof_Glass_Frame` + `Aero_Side_Skirt` + `Aero_Front_Lip` + `Aero_Front_Splitter` + `Aero_Rear_Fender_Flare` + `Aero_Rear_Wing` + `Aero_Roof_Fin` + 4 `Wheel_Front`/`Wheel_Rear` tyres, 10-spoke chrome alloy rims & brake rotors) |
| `Liger_Body_Surface.arc` | Full exterior NURBS body shell (`Body_Main_Shell` + `Body_Front_Cowl` + `Body_Roof_Glass_Frame`) with pearl, carbon, and glass matcaps |
| `Liger_Body_Panels.arc` | Anatomical multi-panel NURBS body segmentation (`Front_Hood`, `Front_Fascia`, `Front_Fenders`, `Canopy_Roof`, `Side_Doors_Pods`, `Rear_Haunches`, `Rear_Engine_Deck`, `Rear_Fascia`, `Front_Cowl`, `Roof_Frame`) |
| `Liger_Body_CurveLoft.arc` | Exterior NURBS body shell overlaid with the 3D crease & feature curve network from `curves.json` |
| `Liger_Body_SideR.arc` | $+Y$ right-hand half-shell NURBS body + $+Y$ 3D feature curve network |
| `Liger_Body_Curves.arc` | Extracted 3D crease, boundary, and orthographic silhouette curve network |

---

## Proof Renders (`Vehicles/Liger/SolidArc/`)

- **Complete Vehicle (`Liger_Complete.arc`)**:
  - `Liger_Complete_01_FrontQuarter.png` — Front-right 3/4 perspective (`plastic` + isoparametric wireframe)
  - `Liger_Complete_02_RearQuarter.png` — Rear-right 3/4 perspective (`plastic` + isoparametric wireframe)
  - `Liger_Complete_03_Side.png` — Orthographic right side elevation
  - `Liger_Complete_04_Top.png` — Orthographic top plan view
  - `Liger_Complete_05_Front.png` — Orthographic front elevation
  - `Liger_Complete_06_ContactSheet.png` — 4-view engineering contact sheet (`Iso`, `Side`, `Front`, `Top`)
  - `Liger_Complete_07_FrontQuarter_Matcap.png` — Front-right 3/4 clean `matcap` render
  - `Liger_Complete_08_RearQuarter_Matcap.png` — Rear-right 3/4 clean `matcap` render
- **Exterior Body Surface (`Liger_Body_Surface.arc`)**: `Liger_SA_01_Iso_Curves.png` .. `Liger_SA_06_Front.png`
- **Segmented Body Panels (`Liger_Body_Panels.arc`)**: `Liger_Panels_01_Iso.png`, `Liger_Panels_02_RearQuarter.png`, `Liger_Panels_04_Top.png`, `Liger_Panels_05_Elev_A.png`, `Liger_Panels_07_FrontQuarter.png`
- **Body + 3D Feature Curves (`Liger_Body_CurveLoft.arc`)**: `Liger_CL_01_Iso.png`, `Liger_CL_02_RearQuarter.png`, `Liger_CL_04_Top.png`, `Liger_CL_05_Elev_A.png`, `Liger_CL_07_FrontQuarter.png`
- **Right-Side Half-Shell (`Liger_Body_SideR.arc`)**: `Liger_SideR_01_RearQuarter.png` .. `Liger_SideR_04_Top.png`

---

## Reproducing from Source

```bash
# 1. (Optional) Re-extract all 11 exterior mesh archives from Liger_named.blend via headless bpy:
LD_LIBRARY_PATH=/tmp/stublibs PYTHONPATH=/tmp/pylib python3 Vehicles/tools/extract_liger_meshes.py

# 2. Reconstruct all 5 SolidArc (.arc) CAD documents from Vehicles/Liger/mesh/*.npz:
PYTHONPATH=/tmp/pylib python3 Vehicles/tools/reconstruct_liger.py

# 3. Render all proof images with SolidArc:
for scr in render_complete.arc render_views.arc render_panels.arc render_curveloft.arc render_sideR.arc; do
    /tmp/solidarc-build/SolidArc "Vehicles/Liger/SolidArc/$scr"
done
```
