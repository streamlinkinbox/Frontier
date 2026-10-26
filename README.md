# Frontier

## Terrain Editor — CliffGenerator

[`Experimental/CliffGenerator/`](Experimental/CliffGenerator/) is the Frontier
terrain generator in the original Frontier Editor UI (outliner · viewport ·
inspector). It was imported as-is first; the only behavioural replacement after
that import is inside its existing steep-slope SDF chunks:

- The simulated heightfield remains authoritative for landform, strata,
  erosion, rivers, materials, rocks and exports.
- Cells steeper than the editor's cliff angle are still selected and budgeted
  exactly as before, and the surrounding heightfield mesh still skips the
  covered quads.
- Inside each selected chunk, the former analytic carve is replaced by a small
  dense SDF volume seeded from that heightfield, filled from the editor's own
  strata/joint context, differentially weathered, and relieved by detachment
  of unsupported joint-bounded blocks.
- The volume is cropped and blended back to exact `y - h` where the chunk
  weight is zero, preserving the original watertight heightfield seam.

```sh
cd Experimental/CliffGenerator
npm install
npm run dev        # Vite on http://localhost:5173/
npm run check      # heightfield generation + SDF chunk checks
```

See [`Experimental/CliffGenerator/README.md`](Experimental/CliffGenerator/README.md)
for the full pipeline, representation and control reference.
