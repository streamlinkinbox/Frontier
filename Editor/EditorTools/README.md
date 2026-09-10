# Editor Tools

| Folder | Tool | Status |
|---|---|---|
| `ParametricSketcher/` | **SolidArc** — parametric NURBS/B-rep modeller (Phases 1–20: kernel, topology, booleans, loft/sweep, fair patches, dimensions, 2D constraint solver, mirror/radial) | active |
| `TextureEditor/` | 3D painting and texture baking | planned |

Build one tool: `cmake -S Editor/EditorTools/<Tool> -B build/<Tool> && cmake --build build/<Tool>`
