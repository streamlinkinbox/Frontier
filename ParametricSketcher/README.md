# SolidArc — parametric NURBS modelling tool (C++20, console + rasterised proofs)

Folder: `ParametricSketcher/`. Application name: **SolidArc**.

A standalone modelling tool that lives beside `Engine/` and `Projects/` and depends on neither. Every piece of
geometry is a NURBS curve or surface; every solid is a B-rep of trimmed NURBS faces; every visual is drawn by a
GPU-shaped renderer (software rasteriser here, Vulkan on a machine with a GPU). No UI toolkit: all data goes to the
console, all visuals go to PNG proofs in `Proofs/`.

## Build & verify

```bash
cd ParametricSketcher
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure      # or run ./build/KernelVerification directly for the full table
```

No external packages. `-Wall -Wextra -Wpedantic -Werror`.

## Layout

| Folder | Role | Status |
|---|---|---|
| `Kernel/` | Pure geometry, zero dependencies (port target for anything) | **Phase 1 ✓** |
| `Interaction/` | `CameraProjection` (Z-up turntable, numpad views, frame, pixel rays) ✓ · workplane, snapping, modal input, gizmo | Phase 2 ✓ / 3–5 |
| `Presentation/` | `RasterExchange` seam · `SoftwareRaster` (CPU, pick + depth, PNG with own deflate) · Slang shaders compiled twice: by Slang for Vulkan later, by the C++ compiler through `SlangMirror.h` today · `ScenePresentation` (kernel → streams) | **Phase 2 ✓** |
| `Console/` | `CommandCodec` (`.arc` grammar) · `ConsoleHost` (sketch, primitives, extrude/revolve/loft, scene, view, `render`, `pick`) · `SolidArc` executable (script / `-c` / REPL) | **Phase 2 ✓** |
| `Document/` | `SceneDocument` — named items with stable identities (pick identities) | **Phase 2 ✓** |
| `Verification/` | One console-proof executable per phase, registered with ctest | ongoing |
| `Scripts/` | Reproducible `.arc` scripts (the visual test suite) | Phase 3+ |
| `Proofs/` | PNG outputs shown after each phase | Phase 2+ |

### Kernel (Phase 1)

| Unit | Contents |
|---|---|
| `ScalarCriteria.h` | The one tolerance policy (`KernelTolerance 1e-9`, `MergeTolerance 1e-6`, `AngularTolerance 1e-7`), `Refusal` / `Deliver<T>` fail-fast values |
| `VectorSpecification.h` | `Vec2/3/4`, `Quat`, column-major `Mat4` (Vulkan clip conventions, Z-up right-handed), `Plane`, `Workplane`, `Ray`, `Box3` |
| `CurveSpecification` | `NurbsCurve`: exact rational Line / Arc / Circle / 3-pt arc / Ellipse / Rectangle (+rounded) / Polygon / Slot, Bézier, control-point B-spline (open + periodic), global interpolation; de Boor, derivatives, curvature, length, closest point (Newton), knot insertion, Bézier decomposition, degree elevation, split / trim / reverse / join, adaptive tessellation |
| `SurfaceSpecification` | `NurbsSurface`: exact Plane / Sphere / Cylinder / Cone / Torus, B-spline patch, Extrusion, Revolution, Ruled, Loft (homogeneous skinning so circles stay exact); derivatives, outward normals (degenerate poles handled), iso-curves, closest point, knot insertion / split in U and V, curvature-adaptive tessellation with CCW triangles |

Winding contract: for every closed primitive `∂S/∂u × ∂S/∂v` points **outward**; tessellations are CCW seen from
outside. Verified numerically in `KernelVerification` — this is what booleans and back-face tinting rely on later.

## Phase plan

| # | Phase | Proof |
|---|---|---|
| 1 | Kernel: vectors, tolerance, NURBS curves & surfaces | `KernelVerification` — 76 checks (radius error < 1e-12, de Boor ≡ Bernstein, refinement invariance, outward normals) |
| 2 | `RasterExchange` + software rasteriser + Slang shaders (grid, line, point, surface) + camera | `Proof_02_Grid.png`, `Proof_02_Sphere.png` |
| 3 | Workplane, snap, modal input, sketch tools (Line … control-point curve) | dimensioned profile with snap markers |
| 4 | Pick pass, selection modes, records, undo/redo, hotkey table | highlighted selection, box select |
| 5 | Gizmo (GizmoPRO design) + G/R/S modal + numeric input | combined and separate T/R/S gizmos |
| 6 | B-rep topology + primitives + shell validation | all primitives, back-face tint |
| 7 | 2D booleans, fillet / chamfer / trim / offset / join | region tables, winding normalised |
| 8 | Extrude / Revolve / Loft / Sweep → solids | extruded profile with hole, revolved vase |
| 9 | Surface–surface intersection + 3D NURBS booleans | box∪box, box−cylinder, sphere∩box, coplanar subtract |
| 10 | Script suite, contact sheet, Vulkan backend hand-off notes | ctest green |

## Console quick start

```bash
./build/SolidArc Scripts/Phase2_Primitives.arc        # run a script; PNGs land in Proofs/
./build/SolidArc -c "circle (0,0) 3; sphere (0,0,1) 1; view iso; view frame; render Quick"
./build/SolidArc                                        # REPL — type help
```

Points are `(x,y)` on the active workplane or `(x,y,z)` in world. Items are addressed by name or `#id`.
